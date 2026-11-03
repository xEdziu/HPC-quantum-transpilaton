import pytest
from qiskit import qasm2
from qiskit.quantum_info import Operator

from qmap.instances import (BASIS, QAOA_SIZES, QFT_SIZES, canonicalize,
                            load_instance, loads, qaoa, qft, SEED)

NAMES = [f"qft_n{n}" for n in QFT_SIZES] + [f"qaoa_n{n}" for n in QAOA_SIZES]


@pytest.mark.parametrize("name", NAMES)
def test_instance_loads(name):
    qc = load_instance(name)
    n = int(name.split("_n")[1])
    assert qc.num_qubits == n
    assert set(qc.count_ops()) <= set(BASIS)


@pytest.mark.parametrize("make", [lambda: qft(8), lambda: qaoa(8, SEED)])
def test_canonical_equivalent_to_original(make):
    original = make()
    assert Operator(original).equiv(Operator(canonicalize(original)))


@pytest.mark.parametrize("name,make", [("qft_n8", lambda: qft(8)),
                                       ("qaoa_n8", lambda: qaoa(8, SEED))])
def test_file_equivalent_to_original(name, make):
    assert Operator(make()).equiv(Operator(load_instance(name)))


@pytest.mark.parametrize("name", ["qft_n8", "qaoa_n8"])
def test_qasm_round_trip(name):
    qc = load_instance(name)
    assert Operator(qc).equiv(Operator(loads(qasm2.dumps(qc))))
