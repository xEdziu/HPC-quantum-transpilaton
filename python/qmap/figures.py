"""Report figures and tables: device topology, example circuits, routing before/after, instance and device tables.

    uv run python -m qmap.figures --out results\\figures

Figures are written as PNG and PDF (vector, for LaTeX); tables as CSV / LaTeX (booktabs).
The routing figure needs the C++ router (build/Release/qmap_eval.exe, or the QMAP_EVAL environment variable).
"""
import argparse
import collections
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import networkx as nx  # noqa: E402
import pandas as pd  # noqa: E402
import qiskit  # noqa: E402
from qiskit import QuantumCircuit, qasm2  # noqa: E402

from qmap import instances  # noqa: E402
from qmap.device import DEVICE_PATH, load_backend, load_device  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SABRE_CSV = ROOT / "results" / "baseline" / "sabre.csv"
ROUTING_QUBITS = 4  # QFT size of the routing figure: small enough to read, needs SWAPs on the identity layout
CIRCUIT_FOLD = 20  # gate columns per row in the circuit drawings

plt.rcParams.update({"font.size": 11, "axes.titlesize": 11, "figure.dpi": 100, "savefig.dpi": 200})


def save(fig, out, name):
    out.mkdir(parents=True, exist_ok=True)
    for ext in ("png", "pdf"):
        fig.savefig(out / f"{name}.{ext}", bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {out / name}.png/.pdf")


# --------------------------------------------------------------------------------------------- tables

def instance_rows(sabre_csv=SABRE_CSV):
    """One row per data/instances circuit: size, gate counts, depth, generator seed, SABRE SWAP min / median."""
    names = [f"qft_n{n}" for n in instances.QFT_SIZES] + [f"qaoa_n{n}" for n in instances.QAOA_SIZES]
    rows = []
    for name in names:
        qc = instances.load_instance(name)
        ops = qc.count_ops()
        total = sum(ops.values())
        rows.append({
            "name": name, "type": "QFT" if name.startswith("qft") else "QAOA", "n": qc.num_qubits, "gates": total,
            "gates_1q": total - ops.get("cx", 0), "cx": ops.get("cx", 0), "depth": qc.depth(),
            "seed": instances.SEED if name.startswith("qaoa") else None,
        })
    df = pd.DataFrame(rows)
    if Path(sabre_csv).exists():
        sabre = pd.read_csv(sabre_csv).groupby("instance")["swaps"].agg(sabre_min="min", sabre_median="median")
        df = df.merge(sabre, left_on="name", right_index=True, how="left")
    return df


def _cell(v):
    if v is None or (isinstance(v, float) and pd.isna(v)):
        return "--"
    if isinstance(v, float):
        return str(int(v)) if v == int(v) else f"{v:.1f}"
    return str(v)


def latex_table(header, rows, align):
    lines = [f"\\begin{{tabular}}{{{align}}}", "\\toprule", " & ".join(header) + " \\\\", "\\midrule"]
    lines += [" & ".join(_cell(c) for c in r) + " \\\\" for r in rows]
    lines += ["\\bottomrule", "\\end{tabular}"]
    return "\n".join(lines) + "\n"


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {path}")


def table_instances(out):
    df = instance_rows()
    out.mkdir(parents=True, exist_ok=True)
    df.to_csv(out / "instances.csv", index=False, lineterminator="\n")
    print(f"wrote {out / 'instances.csv'}")
    header = ["Instancja", "Typ", "$n$", "Bramki", "1q", "CX", "Głębokość", "Ziarno"]
    cols = ["name", "type", "n", "gates", "gates_1q", "cx", "depth", "seed"]
    align = "llrrrrrr"
    if "sabre_min" in df:
        header += ["SABRE min", "SABRE med."]
        cols += ["sabre_min", "sabre_median"]
        align += "rr"
    rows = [[r[c].replace("_", "\\_") if c == "name" else r[c] for c in cols] for _, r in df.iterrows()]
    write_text(out / "instances.tex", latex_table(header, rows, align))


def device_summary():
    """Facts about the exported Falcon 27q device (data/devices/falcon27.json)."""
    data, _ = load_device()
    degree = collections.Counter()
    for a, b in data["edges"]:
        degree[a] += 1
        degree[b] += 1
    import qiskit_ibm_runtime
    return {
        "name": data["name"], "qubits": data["n_phys"], "edges": len(data["edges"]),
        "max_distance": max(max(row) for row in data["distance"]),
        "degree_histogram": dict(sorted(collections.Counter(degree.values()).items())),
        "basis_gates": data["basis_gates"], "qiskit": qiskit.__version__,
        "qiskit_ibm_runtime": qiskit_ibm_runtime.__version__,
    }


def table_device(out):
    s = device_summary()
    hist = ", ".join(f"{k}: {v}" for k, v in s["degree_histogram"].items())
    rows = [
        ["Urządzenie", f"\\texttt{{{s['name']}}} (IBM Falcon)"],
        ["Kubity fizyczne", s["qubits"]],
        ["Krawędzie (nieskierowane)", s["edges"]],
        ["Maks. odległość w grafie", s["max_distance"]],
        ["Histogram stopni (stopień: liczba kubitów)", hist],
        ["Bramki bazowe", ", ".join(f"\\texttt{{{g}}}" for g in s["basis_gates"])],
        ["Wersja \\texttt{qiskit}", s["qiskit"]],
        ["Wersja \\texttt{qiskit-ibm-runtime}", s["qiskit_ibm_runtime"]],
    ]
    write_text(out / "device_falcon27.tex", latex_table(["Parametr", "Wartość"], rows, "ll"))


# ------------------------------------------------------------------------------------------ figures

def figure_topology(out):
    backend = load_backend()
    try:
        from qiskit.visualization import plot_gate_map
        fig = plot_gate_map(backend, figsize=(4.5, 6), qubit_size=34, font_size=13, line_width=4)
    except Exception as e:  # plot_gate_map missing or without coordinates for this backend
        print(f"plot_gate_map failed ({e}); using a networkx layout")
        _, cm = load_device()
        g = nx.Graph([tuple(e) for e in cm.get_edges()])
        fig, ax = plt.subplots(figsize=(4.5, 6))
        nx.draw(g, nx.spring_layout(g, seed=7), ax=ax, with_labels=True, node_color="#4c78a8", font_color="white",
                node_size=520, font_size=11)
    save(fig, out, "topology_falcon27")


def draw_circuit(qc, **kw):
    return qc.draw("mpl", fold=CIRCUIT_FOLD, scale=0.8, **kw)


def figure_circuits(out):
    save(draw_circuit(instances.canonicalize(instances.qft(5))), out, "circuit_qft5")
    save(draw_circuit(instances.canonicalize(instances.qaoa(6))), out, "circuit_qaoa6")


def find_eval():
    env = os.environ.get("QMAP_EVAL")
    path = Path(env) if env else ROOT / "build" / "Release" / "qmap_eval.exe"
    if not path.is_file():
        raise FileNotFoundError(f"qmap_eval not found at {path} (build Release or set QMAP_EVAL)")
    return path


def figure_routing(out):
    """CX skeleton of a small QFT, before and after the C++ router (identity layout); SWAPs are drawn as gates."""
    n = ROUTING_QUBITS
    full = instances.canonicalize(instances.qft(n))
    before = QuantumCircuit(n)
    for inst in full.data:  # keep only the CX gates: the router sees nothing else, and the drawing stays short
        if inst.operation.name == "cx":
            before.cx(*[full.find_bit(q).index for q in inst.qubits])
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = Path(tmp) / "before.qasm", Path(tmp) / "routed.qasm"
        src.write_text(qasm2.dumps(before), encoding="utf-8")
        subprocess.run([str(find_eval()), "--instance", str(src), "--device", str(DEVICE_PATH),
                        "--layout", ",".join(map(str, range(n))), "--out-qasm", str(dst)],
                       check=True, capture_output=True, text=True)
        routed = instances.loads(dst.read_text(encoding="utf-8"))
    cx0 = before.count_ops().get("cx", 0)
    k = routed.count_ops().get("swap", 0)
    fig, (a0, a1) = plt.subplots(2, 1, figsize=(9, 6.5), gridspec_kw={"height_ratios": [1, 1.4]})
    before.draw("mpl", ax=a0, idle_wires=False)
    routed.draw("mpl", ax=a1, idle_wires=False)
    a0.set_title(f"QFT n={n}, CX only, before routing: CX = {cx0}", loc="left")
    a1.set_title(f"after routing (identity initial layout, physical qubits): SWAP = {k}", loc="left")
    fig.suptitle(f"SWAP added: {k},  CX: before {cx0} / after {cx0}+3*{k} = {cx0 + 3 * k} (SWAP = 3 CX)")
    save(fig, out, "routing_before_after")


FIGURES = [("topology", figure_topology), ("circuits", figure_circuits), ("routing", figure_routing)]
TABLES = [("instances", table_instances), ("device", table_device)]


def main(argv=None):
    ap = argparse.ArgumentParser(prog="qmap.figures", description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="results/figures")
    a = ap.parse_args(argv)
    out = Path(a.out)
    failed = []
    for name, fn in FIGURES + TABLES:  # one failure must not block the rest
        try:
            fn(out)
        except Exception as e:
            print(f"[FAIL] {name}: {e!r}", file=sys.stderr)
            failed.append(name)
    if failed:
        raise SystemExit(f"failed: {', '.join(failed)}")


if __name__ == "__main__":
    main()
