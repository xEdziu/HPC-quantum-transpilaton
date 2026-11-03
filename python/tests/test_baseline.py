import os

import pytest
from qiskit import qasm2

from qmap import baseline
from qmap.baseline import build_pass_manager, run_sabre
from qmap.device import load_device
from qmap.instances import load_instance
from qmap.validate import validate

_, CM = load_device()


@pytest.fixture(scope="module")
def original():
    return load_instance("qft_n8")


def test_same_seed_is_identical(original):
    a, b = run_sabre(original, CM, 3), run_sabre(original, CM, 3)
    assert a["swaps"] == b["swaps"]
    assert a["initial"] == b["initial"]
    assert a["final"] == b["final"]
    assert qasm2.dumps(a["routed"]) == qasm2.dumps(b["routed"])


def test_output_validates(original):
    res = run_sabre(original, CM, 0)
    assert res["swaps"] > 0
    assert validate(original, res["routed"], res["initial"], res["final"], CM, k=1)["valid"]


def test_pass_manager_has_only_sabre_layout():
    tasks = build_pass_manager(CM, 0).to_flow_controller().tasks
    assert [type(t).__name__ for t in tasks] == ["SabreLayout"]


def test_environment_is_pinned():
    assert baseline.SABRE_PARAMS["layout_trials"] == 20
    assert "RAYON_NUM_THREADS" in os.environ
    assert "QISKIT_SABRE_ALL_THREADS" not in os.environ
