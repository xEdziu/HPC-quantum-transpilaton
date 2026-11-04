import json
import math

import pandas as pd
import pytest

from qmap import experiments as ex

CLUSTER = {
    "repo_path": "E:\\repo", "exe": "build\\Release\\qmap_ga.exe", "device": "data\\devices\\falcon27.json",
    "hosts": [
        {"name": "rtx5080", "host": "HOST_A", "pop": 1000, "gpu": "x"},
        {"name": "rtx5070", "host": "HOST_B", "pop": 400, "gpu": "x"},
        {"name": "rtx3060", "host": "HOST_C", "pop": 200, "gpu": "x"},
    ],
}


def plan_text(configs, instances=("qft_n8", "qft_n12"), reps=3):
    targets = {(i, ex.eps_tag(e)): 10 + k for i in instances for k, e in enumerate(ex.DEFAULT_EPS)}
    return ex.build_script(CLUSTER, list(instances), configs, targets, reps, 60, "adaptiveB", "all", "results\\runs")


def test_plan_command_count_and_pops():
    text = plan_text(ex.DEFAULT_CONFIGS)
    mpi = [ln for ln in text.splitlines() if ln.lstrip().startswith("mpiexec") and "Tee-Object" in ln]
    assert len(mpi) == 4 * 2 * 3 * 3  # configs x instances x eps x reps
    full = next(ln for ln in mpi if "rtx5080+rtx5070+rtx3060\\qft_n8\\eps0.10\\rep2" in ln)
    assert [p.split()[0] for p in full.split("--pop ")[1:]] == ["1000", "400", "200"]
    assert full.count("--summary") == 1  # rank 0 only
    assert full.count(" : ") == 2
    assert "-host HOST_A" in full.split(" : ")[0]
    assert "--seed 2" in full


def test_plan_run_dirs_distinct_seeds_paired():
    text = plan_text(["rtx5080", "rtx5080+rtx3060"])
    dirs = [ln.split("Test-Path ")[1].split("\\summary.json")[0] for ln in text.splitlines() if "Test-Path" in ln]
    assert len(dirs) == len(set(dirs)) == 2 * 2 * 3 * 3
    # same seed for the same rep in every config
    seeds = {}
    for ln in text.splitlines():
        if ln.lstrip().startswith("mpiexec") and "Tee-Object" in ln:
            d = ln.split("Tee-Object ")[1].split("\\stdout.txt")[0]
            cfg, inst, tag, rep = d.split("\\")[2:]
            seeds.setdefault((inst, tag, rep), set()).add(ln.split("--seed ")[1].split()[0])
    assert all(len(v) == 1 for v in seeds.values())


def test_plan_script_is_ascii_and_resumable():
    text = plan_text(ex.DEFAULT_CONFIGS)
    text.encode("ascii")
    assert "\r" not in text
    assert text.count("if (-not (Test-Path") == text.count("Tee-Object")


def test_pilot_targets_ceil():
    assert ex.target_for(10, 0.10) == 11  # not 12: (1 + 0.1) * 10 = 11.000000000000002
    assert ex.target_for(10, 0.05) == 11
    assert ex.target_for(51, 0.02) == 53
    assert ex.target_for(150, 0.02) == 153
    summaries = [{"instance": "data\\instances\\qft_n8.qasm", "best": 12}, {"instance": "x/qft_n8.qasm", "best": 10},
                 {"instance": "qft_n12.qasm", "best": 100}]
    rows = ex.compute_targets(summaries, [0.10, 0.02])
    assert [(r["instance"], r["B"], r["eps"], r["target"]) for r in rows] == [
        ("qft_n12", 100, "0.10", 110), ("qft_n12", 100, "0.02", 102),
        ("qft_n8", 10, "0.10", 11), ("qft_n8", 10, "0.02", 11)]


def test_censored_median():
    assert ex.censored_median([1, 2, 3], 5) == 3  # 3 of 5: 3rd smallest success
    assert math.isnan(ex.censored_median([1, 2], 5))
    assert ex.censored_median([1, 2], 4) == 2  # lower median
    assert ex.censored_median([5, 1, 3, 2, 4], 5) == 3


def make_df(data):
    rows = [dict(config=c, instance="a", eps="eps0.10", rep=i, best=7, target=8,
                 ttt_s=math.nan if t is None else t, throughput=thr, valid=None)
            for c, ttts, thr in data for i, t in enumerate(ttts)]
    return pd.DataFrame(rows)


def test_aggregate_speedup_and_efficiency():
    df = make_df([("rtx5080", [4, 2, 3, 9, 8], 100.0), ("rtx5080+rtx3060", [1, 2, 3, None, None], 150.0),
                  ("rtx5080+rtx5070", [1, None, None, None, None], 200.0)])
    agg = ex.aggregate_runs(df).set_index("config")
    base = agg.loc["rtx5080"]
    assert (base["n"], base["successes"], base["median_ttt_s"], base["speedup"]) == (5, 5, 4, 1)
    two = agg.loc["rtx5080+rtx3060"]
    assert two["success_rate"] == pytest.approx(0.6)
    assert two["median_ttt_s"] == 3
    assert (two["q1_ttt_s"], two["q3_ttt_s"]) == (1.5, 2.5)
    assert two["speedup"] == pytest.approx(4 / 3)
    assert two["efficiency"] == pytest.approx((4 / 3) / 1.5)
    assert math.isnan(agg.loc["rtx5080+rtx5070", "median_ttt_s"])  # 1 of 5 succeeded
    assert math.isnan(agg.loc["rtx5080+rtx5070", "speedup"])


def test_load_runs_and_quality(tmp_path):
    for cfg, rep, ttt in [("rtx5080", 1, 2.0), ("rtx5080", 2, None)]:
        d = tmp_path / "runs" / cfg / "qft_n8" / "eps0.10" / f"rep{rep}"
        d.mkdir(parents=True)
        (d / "summary.json").write_text(json.dumps(
            {"best": 20 - rep, "ttt_s": ttt, "target": 25, "n_ranks": 2, "instance": "qft_n8"}), encoding="utf-8")
        for r, rate in [(0, 100.0), (1, 50.0)]:
            pd.DataFrame({"evals_per_s": [rate, rate]}).to_csv(d / f"run_r{r}.csv", index=False)
    df = ex.load_runs(tmp_path / "runs")
    assert list(df["throughput"]) == [150.0, 150.0]
    assert df["ttt_s"].isna().tolist() == [False, True]
    sabre = tmp_path / "sabre.csv"
    sabre.write_text("instance,swaps\nqft_n8,10\nqft_n8,14\n", encoding="utf-8")
    q = ex.quality_table(df, sabre).iloc[0]
    assert (q["ga_best"], q["sabre_min"], q["sabre_median"]) == (18, 10, 12)
