import pytest
from qiskit import QuantumCircuit
from qiskit.circuit.library import RZGate
from qiskit.transpiler import PassManager
from qiskit.transpiler.passes import SabreLayout

from qmap.device import load_device
from qmap.instances import load_instance
from qmap.validate import (check_map, check_statevector, check_structural,
                           validate)

N_PHYS = 27
_, CM = load_device()


@pytest.fixture(scope="module", params=["qft_n8", "qaoa_n8"])
def case(request):
    original = load_instance(request.param)
    pm = PassManager([SabreLayout(CM, seed=1, max_iterations=2, layout_trials=4, swap_trials=4)])
    out = pm.run(original)
    assert out.num_qubits == N_PHYS
    layout = out.layout
    return (original, out, layout.initial_index_layout(filter_ancillas=True),
            layout.final_index_layout())


def _first_swap(routed):
    return next(i for i, inst in enumerate(routed.data) if inst.operation.name == "swap")


def _copy_with(routed, skip=None, replace=None):
    out = routed.copy_empty_like()
    for i, inst in enumerate(routed.data):
        if i == skip:
            continue
        op = replace[1] if replace and replace[0] == i else inst.operation
        out.append(op, inst.qubits)
    return out


def test_sabre_output_valid(case):
    original, routed, initial, final = case
    assert routed.count_ops().get("swap", 0) > 0
    report = validate(original, routed, initial, final, CM)
    assert report["valid"] is True, report


def test_hand_made_nonadjacent_cx():
    a = 0
    m = CM.neighbors(a)[0]
    b = next(x for x in CM.neighbors(m) if x != a)
    original = QuantumCircuit(2)
    original.sx(0)
    original.cx(0, 1)
    original.rz(0.3, 1)
    routed = QuantumCircuit(N_PHYS)
    routed.sx(a)
    routed.swap(b, m)
    routed.cx(a, m)
    routed.rz(0.3, m)
    report = validate(original, routed, [a, b], [a, m], CM)
    assert report["valid"] is True, report
    assert report["swaps"] == 1


def test_negative_removed_swap(case):
    original, routed, initial, final = case
    bad = _copy_with(routed, skip=_first_swap(routed))
    assert not check_structural(original, bad, initial, final)
    assert not check_statevector(original, bad, initial, final)[0]
    assert validate(original, bad, initial, final, CM)["valid"] is False


def test_negative_wrong_final_layout(case):
    original, routed, initial, final = case
    bad_final = list(final)
    bad_final[0], bad_final[1] = bad_final[1], bad_final[0]
    assert not check_structural(original, routed, initial, bad_final)
    assert not check_statevector(original, routed, initial, bad_final)[0]
    assert validate(original, routed, initial, bad_final, CM)["valid"] is False


def test_negative_changed_rz_angle(case):
    original, routed, initial, final = case
    i = next(i for i, inst in enumerate(routed.data) if inst.operation.name == "rz")
    angle = float(routed.data[i].operation.params[0])
    bad = _copy_with(routed, replace=(i, RZGate(angle + 0.1)))
    assert not check_structural(original, bad, initial, final)
    assert not check_statevector(original, bad, initial, final)[0]
    assert validate(original, bad, initial, final, CM)["valid"] is False


def test_check_map_rejects_non_edge():
    non_edge = next((p, q) for p in range(N_PHYS) for q in range(p + 1, N_PHYS)
                    if not CM.graph.has_edge(p, q) and not CM.graph.has_edge(q, p))
    qc = QuantumCircuit(N_PHYS)
    qc.cx(*non_edge)
    assert not check_map(qc, CM)
    edge = sorted(CM.get_edges())[0]
    ok = QuantumCircuit(N_PHYS)
    ok.cx(*edge)
    assert check_map(ok, CM)
