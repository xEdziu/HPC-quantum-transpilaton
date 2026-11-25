import json
import math

import pandas as pd
import pytest

from qmap import experiments as ex

CLUSTER = {
    "repo_path": "E:\\repo", "exe": "build\\Release\\qmap_ga.exe", "device": "data\\devices\\falcon27.json",
    "netmask": "192.168.66.0/255.255.255.0",
    "hosts": [
        {"name": "rtx5080", "host": "HOST_A", "pop": 1000, "gpu": "x"},
        {"name": "rtx5070", "host": "HOST_B", "pop": 400, "gpu": "x"},
        {"name": "rtx3060", "host": "HOST_C", "pop": 200, "gpu": "x"},
    ],
}


def plan_text(configs, instances=("qft_n8", "qft_n12"), reps=3, multi=True):
    # eps 0.10 / 0.05 / 0.02 -> targets 12 / 11 / 10 (strictest last)
    targets = {(i, ex.eps_tag(e)): 12 - k for i in instances for k, e in enumerate(ex.DEFAULT_EPS)}
    return ex.build_script(CLUSTER, list(instances), configs, targets, reps, 60, "adaptiveB", "all", "results\\runs",
                           multi=multi)


def test_plan_command_count_and_pops():
    text = plan_text(ex.DEFAULT_CONFIGS)
    mpi = [ln for ln in text.splitlines() if ln.lstrip().startswith("mpiexec") and "Tee-Object" in ln]
    assert len(mpi) == 4 * 2 * 3  # configs x instances x reps: one run per rep with all eps targets
    full = next(ln for ln in mpi if "rtx5080+rtx5070+rtx3060\\qft_n8\\rep2" in ln)
    assert full.count("--target 12,11,10 ") == 3  # same list on every rank
    assert [p.split()[0] for p in full.split("--pop ")[1:]] == ["1000", "400", "200"]
    assert full.count("--summary") == 1  # rank 0 only
    assert full.count(" : ") == 2
    assert "-host HOST_A" in full.split(" : ")[0]
    assert "--seed 2" in full
    # every rank: no generation cap, so only --max-time or the target ends a run
    assert full.count(f"--generations {ex.NO_GENERATION_CAP}") == 3
    # global options before the first section: -timeout (hung run) and -genv netmask (reaches every MPMD section)
    assert full.lstrip().startswith("mpiexec -timeout 120 -genv MPICH_NETMASK 192.168.66.0/255.255.255.0 -n 1")


def test_plan_timeout_and_failed_run_log():
    assert ex.mpiexec_timeout(120) == 180
    assert ex.mpiexec_timeout(0.5) == 1 + ex.MPIEXEC_TIMEOUT_MARGIN_S
    lines = plan_text(["rtx5080"], instances=("qft_n8",), reps=1).splitlines()
    i = next(k for k, ln in enumerate(lines) if "Tee-Object" in ln)
    # the line right after mpiexec records a non-zero exit (timeout, lost host) before the retry overwrites stdout.txt
    assert lines[i + 1].strip() == ('if ($LASTEXITCODE -ne 0) { Add-Content results\\runs\\failed_runs.log '
                                    '"$(Get-Date -Format s) exit=$LASTEXITCODE results\\runs\\rtx5080\\qft_n8\\rep1" }')
    assert lines[i + 2].strip() == "}"


def test_plan_run_dirs_distinct_seeds_paired():
    text = plan_text(["rtx5080", "rtx5080+rtx3060"])
    dirs = [ln.split("Test-Path ")[1].split("\\summary.json")[0] for ln in text.splitlines() if "Test-Path" in ln]
    assert len(dirs) == len(set(dirs)) == 2 * 2 * 3
    # same seed for the same rep in every config
    seeds = {}
    for ln in text.splitlines():
        if ln.lstrip().startswith("mpiexec") and "Tee-Object" in ln:
            d = ln.split("Tee-Object ")[1].split("\\stdout.txt")[0]
            cfg, inst, rep = d.split("\\")[2:]
            seeds.setdefault((inst, rep), set()).add(ln.split("--seed ")[1].split()[0])
    assert all(len(v) == 1 for v in seeds.values())


def test_plan_explicit_target_keeps_tag_layout():
    text = ex.build_script(CLUSTER, ["qft_n8"], ["rtx5080"], {("qft_n8", "target25"): 25}, 2, 60, "adaptiveB", "all",
                           "results\\runs")
    mpi = [ln for ln in text.splitlines() if "Tee-Object" in ln]
    assert len(mpi) == 2
    assert "results\\runs\\rtx5080\\qft_n8\\target25\\rep1" in mpi[0] and "--target 25 " in mpi[0]


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


def test_censored_quartiles_ordered():
    # 5 of 10 succeeded: q1 = 3rd, median = 5th, q3 = 8th (censored)
    ok = [10, 20, 30, 40, 118]
    assert ex.censored_quantile(ok, 10, 0.25) == 30
    assert ex.censored_quantile(ok, 10, 0.5) == 118
    assert math.isnan(ex.censored_quantile(ok, 10, 0.75))
    # sorted 2,3,4,8,9: q1 = 2nd (ceil(1.25)), q3 = 4th (ceil(3.75))
    assert ex.censored_quantile([4, 2, 3, 9, 8], 5, 0.25) == 3 and ex.censored_quantile([4, 2, 3, 9, 8], 5, 0.75) == 8


def test_load_runs_multi_target_censoring(tmp_path):
    targets = {("qft_n8", "eps0.10"): 12, ("qft_n8", "eps0.05"): 11, ("qft_n8", "eps0.02"): 10}
    # two reps of one config; rep2 never reaches the strictest target
    for rep, by in [(1, {"12": 0.5, "11": 1.0, "10": 2.0}), (2, {"12": 0.7, "11": 1.5, "10": None})]:
        d = tmp_path / "runs" / "rtx5080" / "qft_n8" / f"rep{rep}"
        d.mkdir(parents=True)
        (d / "summary.json").write_text(json.dumps(
            {"best": 10 + rep, "ttt_s": by["10"], "target": 10, "targets": [12, 11, 10], "ttt_by_target": by,
             "n_ranks": 1, "instance": "qft_n8"}), encoding="utf-8")
        pd.DataFrame({"evals_per_s": [100.0]}).to_csv(d / "run_r0.csv", index=False)
    df = ex.load_runs(tmp_path / "runs", targets)
    assert len(df) == 2 * 3  # reps x eps
    agg = ex.aggregate_runs(df).set_index("eps")
    assert agg.loc["eps0.10", "successes"] == 2 and agg.loc["eps0.10", "median_ttt_s"] == 0.5
    assert agg.loc["eps0.02", "successes"] == 1 and agg.loc["eps0.02", "success_rate"] == 0.5
    assert agg.loc["eps0.02", "median_ttt_s"] == 2.0  # lower median of 2 runs, one censored
    assert set(agg["target"]) == {12, 11, 10}
    assert df["throughput"].eq(100.0).all()
    with pytest.raises(SystemExit):
        ex.load_runs(tmp_path / "runs")  # multi-target layout needs targets.csv


def test_parse_nvsmi(tmp_path):
    f = tmp_path / "nvsmi.csv"
    f.write_text("timestamp, temperature.gpu, clocks.current.sm [MHz], power.draw [W], utilization.gpu [%], "
                 "clocks_throttle_reasons.active\n"
                 "2026/11/22 14:00:00.500, 61, 2400 MHz, 250.25 W, 99 %, 0x0000000000000000\n"
                 "2026/11/22 14:00:01.000, 63, 1800 MHz, 240.00 W, 98 %, 0x0000000000000080\n", encoding="utf-8")
    d = ex.parse_nvsmi(f)
    assert list(d["clock_sm"]) == [2400, 1800] and list(d["temp"]) == [61, 63]
    assert d["power"].iloc[0] == pytest.approx(250.25) and list(d["util"]) == [99, 98]
    assert d["reasons"].iloc[1] == "0x0000000000000080"
    assert (d["time"].iloc[1] - d["time"].iloc[0]).total_seconds() == 0.5


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
    assert two["q1_ttt_s"] == 2  # 2nd of 5 (ceil(0.25*5))
    assert math.isnan(two["q3_ttt_s"])  # 4th of 5 is censored
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


def _nsys_db(path):
    """Minimal nsys SQLite export: 2 generations of 10 ms, each with a 4 ms evaluation and a 0.5 ms migration."""
    import sqlite3
    ms = 1_000_000
    nvtx = [("ga_run", 0, 20 * ms)]
    for g in range(2):
        b = g * 10 * ms
        nvtx += [("ga_evaluate", b, b + 4 * ms), ("fitness_h2d", b, b + ms), ("fitness_kernel", b + ms, b + 3 * ms),
                 ("fitness_d2h", b + 3 * ms, b + int(3.5 * ms)), ("mpi_migrate", b + 4 * ms, b + int(4.5 * ms))]
    with sqlite3.connect(path) as c:
        c.execute("CREATE TABLE NVTX_EVENTS (start INT, end INT, text TEXT)")
        c.executemany("INSERT INTO NVTX_EVENTS VALUES (?, ?, ?)", [(s, e, n) for n, s, e in nvtx])
        c.execute("INSERT INTO NVTX_EVENTS VALUES (5, NULL, 'marker')")  # instant event: ignored
        c.execute("CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL (start INT, end INT)")
        c.execute("CREATE TABLE CUPTI_ACTIVITY_KIND_MEMCPY (start INT, end INT, copyKind INT)")
        c.executemany("INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES (?, ?)", [(ms, 3 * ms), (11 * ms, 13 * ms)])
        c.executemany("INSERT INTO CUPTI_ACTIVITY_KIND_MEMCPY VALUES (?, ?, ?)",
                      [(0, ms, 1), (3 * ms, 3 * ms + 10, 2), (0, 5, 8)])


def test_generation_breakdown(tmp_path):
    db = tmp_path / "nsys_X.sqlite"
    _nsys_db(db)
    row = ex.generation_breakdown(ex.nsys_ranges(db))
    assert row["generations"] == 2
    assert row["generation_ms"] == pytest.approx(10.0)
    assert row["fitness_h2d"] == pytest.approx(1.0)
    assert row["fitness_kernel"] == pytest.approx(2.0)
    assert row["fitness_d2h"] == pytest.approx(0.5)
    assert row["evaluate_other"] == pytest.approx(0.5)
    assert row["mpi_migrate"] == pytest.approx(0.5)
    assert row["ga_operators"] == pytest.approx(5.5)  # 10 - 4 (evaluate) - 0.5 (migrate)
    assert row["host_share_pct"] == pytest.approx(55.0)
    gpu = ex.nsys_gpu(db)
    assert list(gpu["kind"]) == ["H2D", "kernel", "D2H", "kernel"]  # device-to-device copy (kind 8) dropped


def test_prof_label():
    cluster = {"hosts": [{"name": "rtx5080", "gpu": "RTX 5080", "hostname": "AMG-NORTH"},
                         {"name": "rtx3060", "gpu": "RTX 3060", "hostname": "PS04"}]}
    hosts = ["AMG-NORTH", "PS04"]
    assert ex.prof_label("nsys_AMG-NORTH", cluster, hosts) == "RTX 5080"
    assert ex.prof_label("nsys_ps04", cluster, hosts) == "RTX 3060"
    assert ex.prof_label("nsys3_1", cluster, hosts) == "3 hosts, rank 1: RTX 3060"
    assert ex.prof_label("v1_qft_n12_pop1m", cluster, hosts) is None


def test_prof_plot_outputs(tmp_path):
    prof = tmp_path / "prof"
    prof.mkdir()
    import sqlite3
    ms = 1_000_000
    for stem in ("nsys_AMG-NORTH", "nsys3_0"):
        with sqlite3.connect(prof / f"{stem}.sqlite") as c:
            c.execute("CREATE TABLE NVTX_EVENTS (start INT, end INT, text TEXT)")
            c.execute("CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL (start INT, end INT)")
            c.execute("CREATE TABLE CUPTI_ACTIVITY_KIND_MEMCPY (start INT, end INT, copyKind INT)")
            rows = [(0, 20 * 10 * ms, "ga_run")]
            for g in range(20):
                b = g * 10 * ms
                rows += [(b, b + 4 * ms, "ga_evaluate"), (b + ms, b + 3 * ms, "fitness_kernel")]
                c.execute("INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES (?, ?)", (b + ms, b + 3 * ms))
            c.executemany("INSERT INTO NVTX_EVENTS VALUES (?, ?, ?)", rows)
    pd.DataFrame({"elapsed_s": [0.1, 0.2], "migrants_sent": [0, 2], "migrants_received": [1, 1]}).to_csv(
        prof / "run3_r0.csv", index=False)
    cluster = tmp_path / "cluster.json"
    cluster.write_text(json.dumps({"hosts": [{"name": "rtx5080", "gpu": "RTX 5080", "hostname": "AMG-NORTH"}]}))
    out = tmp_path / "plots"
    ex.main(["prof-plot", "--prof", str(prof), "--out", str(out), "--cluster", str(cluster),
             "--rank-hosts", "AMG-NORTH", "--ncu-python", str(tmp_path / "missing")])
    for name in ("prof_breakdown.csv", "prof_breakdown.png", "prof_timeline.png", "prof_3hosts.png"):
        assert (out / name).exists(), name
    bd = pd.read_csv(out / "prof_breakdown.csv")
    assert list(bd["label"]) == ["RTX 5080", "3 hosts, rank 0: RTX 5080"]
    assert bd["ga_operators"].iloc[0] == pytest.approx(6.0)
