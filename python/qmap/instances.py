"""Canonical QFT and QAOA benchmark circuits in the device-native basis."""
from pathlib import Path

import networkx as nx
from qiskit import QuantumCircuit, qasm2, transpile
from qiskit.synthesis import synth_qft_full

INSTANCES_DIR = Path(__file__).resolve().parents[2] / "data" / "instances"
BASIS = ["rz", "sx", "x", "cx"]
QFT_SIZES = [8, 12, 16, 20, 24, 27]
QAOA_SIZES = [8, 12, 16, 20, 24, 26]  # 3-regular graph needs even n
SEED = 2026
GAMMA, BETA = 0.7, 0.3  # fixed angles; irrelevant for routing


def qft(n):
    return synth_qft_full(n, do_swaps=False)


def qaoa(n, seed=SEED):
    graph = nx.random_regular_graph(3, n, seed=seed)
    qc = QuantumCircuit(n)
    qc.h(range(n))
    for a, b in graph.edges():
        qc.rzz(2 * GAMMA, a, b)
    qc.rx(2 * BETA, range(n))
    return qc


def canonicalize(qc):
    out = transpile(qc, basis_gates=BASIS, optimization_level=0, seed_transpiler=0)
    assert set(out.count_ops()) <= set(BASIS), out.count_ops()
    return out


def write_all(directory=INSTANCES_DIR):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    jobs = [(f"qft_n{n}", qft(n)) for n in QFT_SIZES]
    jobs += [(f"qaoa_n{n}", qaoa(n)) for n in QAOA_SIZES]
    for name, qc in jobs:
        text = qasm2.dumps(canonicalize(qc))
        assert "gate " not in text, name
        with open(directory / f"{name}.qasm", "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


# qasm2.load defaults to the strict OpenQASM 2.0 qelib1.inc, which lacks `sx`;
# the legacy set matches the gate definitions that qasm2.dumps emits.
def loads(text):
    return qasm2.loads(text, custom_instructions=qasm2.LEGACY_CUSTOM_INSTRUCTIONS)


def load_instance(name, directory=INSTANCES_DIR):
    return loads((Path(directory) / f"{name}.qasm").read_text(encoding="utf-8"))


if __name__ == "__main__":
    write_all()
