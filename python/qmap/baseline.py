"""Deterministic Qiskit SABRE baseline with every hidden optimization disabled.

The pass manager contains a single SabreLayout pass (layout + routing + embedding onto
the device); nothing else touches the circuit, so `swap` gates stay countable.
Layout sidecar format is documented in docs/walidacja.md.
"""
import os

# Trial counts and thread usage must not depend on the machine. QISKIT_SABRE_ALL_THREADS
# would raise the preset trial counts to the CPU count; QISKIT_PARALLEL=FALSE disables
# Qiskit's own process pool. RAYON_NUM_THREADS is read when the Rust thread pool starts,
# so it must be set before qiskit is imported.
os.environ.pop("QISKIT_SABRE_ALL_THREADS", None)
os.environ.setdefault("RAYON_NUM_THREADS", "1")
os.environ["QISKIT_PARALLEL"] = "FALSE"

import argparse  # noqa: E402
import csv  # noqa: E402
import json  # noqa: E402
import statistics  # noqa: E402
import time  # noqa: E402
from pathlib import Path  # noqa: E402

import qiskit  # noqa: E402
from qiskit import qasm2  # noqa: E402
from qiskit.transpiler import PassManager  # noqa: E402
from qiskit.transpiler.passes import SabreLayout  # noqa: E402
from qiskit.transpiler.preset_passmanagers import generate_preset_pass_manager  # noqa: E402

from qmap.device import load_backend, load_device  # noqa: E402
from qmap.instances import QAOA_SIZES, QFT_SIZES, load_instance  # noqa: E402
from qmap.validate import validate  # noqa: E402

# Mirrors SabreLayout in optimization_level=3 of qiskit 2.6.1 (builtin_plugins.py).
SABRE_PARAMS = {"max_iterations": 4, "layout_trials": 20, "swap_trials": 20}
ALLOWED_OPS = {"rz", "sx", "x", "cx", "swap"}
ALL_INSTANCES = [f"qft_n{n}" for n in QFT_SIZES] + [f"qaoa_n{n}" for n in QAOA_SIZES]
FIELDS = ["instance", "n", "seed", "swaps", "depth", "two_qubit_count", "seconds",
          "qiskit_version", "valid", "level3_cx", "level3_depth"]


def build_pass_manager(coupling_map, seed):
    return PassManager([SabreLayout(coupling_map, seed=seed, **SABRE_PARAMS)])


def run_sabre(original, coupling_map, seed):
    pm = build_pass_manager(coupling_map, seed)
    t0 = time.perf_counter()
    out = pm.run(original)
    seconds = time.perf_counter() - t0
    ops = out.count_ops()
    assert set(ops) <= ALLOWED_OPS, ops
    return {
        "routed": out,
        "initial": list(out.layout.initial_index_layout(filter_ancillas=True)),
        "final": list(out.layout.final_index_layout()),
        "swaps": ops.get("swap", 0),
        "depth": out.depth(),
        "two_qubit_count": ops.get("cx", 0) + ops.get("swap", 0),
        "seconds": seconds,
    }


def run_level3_context(original, seed):
    """Context only: swaps are decomposed and optimized away, never a TTT target."""
    pm = generate_preset_pass_manager(optimization_level=3, backend=load_backend(),
                                      seed_transpiler=seed)
    out = pm.run(original)
    return {"cx": out.count_ops().get("cx", 0), "depth": out.depth()}


def _write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main():
    ap = argparse.ArgumentParser(description="Run the SABRE baseline.")
    ap.add_argument("--instances", nargs="+", default=ALL_INSTANCES)
    ap.add_argument("--seeds", nargs="+", type=int, default=list(range(10)))
    ap.add_argument("--out", default="results/baseline")
    ap.add_argument("--validate", action="store_true",
                    help="run qmap.validate with k=1 (one random V keeps n=27 affordable)")
    ap.add_argument("--context-level3", action="store_true")
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    _, cm = load_device()
    csv_path = out_dir / "sabre.csv"
    new_file = not csv_path.exists()
    swaps_by_instance = {}
    with open(csv_path, "a", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, FIELDS, lineterminator="\n")
        if new_file:
            writer.writeheader()
        for name in args.instances:
            original = load_instance(name)
            for seed in args.seeds:
                res = run_sabre(original, cm, seed)
                stem = f"{name}_s{seed}"
                _write(out_dir / f"{stem}.qasm", qasm2.dumps(res["routed"]))
                _write(out_dir / f"{stem}.layout.json", json.dumps(
                    {"initial": res["initial"], "final": res["final"], "swaps": res["swaps"]}) + "\n")
                valid = ""
                if args.validate:
                    valid = validate(original, res["routed"], res["initial"], res["final"], cm, k=1)["valid"]
                ctx = run_level3_context(original, seed) if args.context_level3 else {}
                writer.writerow({
                    "instance": name, "n": original.num_qubits, "seed": seed,
                    "swaps": res["swaps"], "depth": res["depth"],
                    "two_qubit_count": res["two_qubit_count"],
                    "seconds": f"{res['seconds']:.3f}", "qiskit_version": qiskit.__version__,
                    "valid": valid, "level3_cx": ctx.get("cx", ""), "level3_depth": ctx.get("depth", ""),
                })
                f.flush()
                swaps_by_instance.setdefault(name, []).append(res["swaps"])
    for name, swaps in swaps_by_instance.items():
        print(f"{name}: min={min(swaps)} median={statistics.median(swaps)} (seeds={len(swaps)})")


if __name__ == "__main__":
    main()
