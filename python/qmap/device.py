"""Export and load the IBM Falcon 27q device description."""
import json
from pathlib import Path

from qiskit.transpiler import CouplingMap
from qiskit_ibm_runtime.fake_provider import FakeKolkataV2

DEVICE_PATH = Path(__file__).resolve().parents[2] / "data" / "devices" / "falcon27.json"
NATIVE_GATES = ("rz", "sx", "x", "cx")


def load_backend():
    return FakeKolkataV2()


def export(path=DEVICE_PATH):
    backend = load_backend()
    directed = {tuple(e) for e in backend.coupling_map.get_edges()}
    symmetrized = any((b, a) not in directed for a, b in directed)
    edges = sorted({tuple(sorted(e)) for e in directed})
    cm = CouplingMap([list(e) for e in edges])
    cm.make_symmetric()
    basis = [g for g in NATIVE_GATES if g in backend.operation_names]
    assert basis == list(NATIVE_GATES), basis
    data = {
        "name": backend.name,
        "n_phys": backend.num_qubits,
        "basis_gates": basis,
        "symmetrized": symmetrized,
        "edges": [list(e) for e in edges],
        "distance": cm.distance_matrix.astype(int).tolist(),
    }
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")


def load_device(path=DEVICE_PATH):
    """Return (device dict, CouplingMap with both directions of every edge)."""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    cm = CouplingMap([list(e) for e in data["edges"]])
    cm.make_symmetric()
    return data, cm


if __name__ == "__main__":
    export()
