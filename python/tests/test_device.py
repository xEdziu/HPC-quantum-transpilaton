import numpy as np
from qiskit.transpiler import CouplingMap

from qmap.device import load_device


def test_device_consistent():
    dev, cm = load_device()
    assert dev["n_phys"] == 27
    assert dev["basis_gates"] == ["rz", "sx", "x", "cx"]
    edges = {tuple(e) for e in dev["edges"]}
    assert all(a < b for a, b in edges)
    assert edges == {tuple(sorted(e)) for e in cm.get_edges()}
    # every directed edge in the loaded map has its reverse
    directed = {tuple(e) for e in cm.get_edges()}
    assert all((b, a) in directed for a, b in directed)


def test_distance_matrix():
    dev, _ = load_device()
    both = [list(e) for e in dev["edges"]] + [[b, a] for a, b in dev["edges"]]
    expected = CouplingMap(both).distance_matrix
    dist = np.array(dev["distance"])
    assert dist.shape == (27, 27)
    assert (dist == expected).all()
    edges = {tuple(e) for e in dev["edges"]}
    for a in range(27):
        for b in range(27):
            assert (dist[a, b] == 1) == (tuple(sorted((a, b))) in edges)
