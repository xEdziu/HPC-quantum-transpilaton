"""Cross-check: C++ router output must pass the Python validator."""
import json
import os
import subprocess
from pathlib import Path

import numpy as np
import pytest

from qmap.device import load_device
from qmap.instances import load_instance, loads
from qmap.validate import validate

ROOT = Path(__file__).resolve().parents[2]
DEVICE_JSON = ROOT / "data" / "devices" / "falcon27.json"
N_PHYS = 27


def _find_eval():
    env = os.environ.get("QMAP_EVAL")
    path = Path(env) if env else ROOT / "build" / "Release" / "qmap_eval.exe"
    return path if path.is_file() else None


EVAL = _find_eval()
pytestmark = pytest.mark.skipif(EVAL is None, reason="qmap_eval not built (set QMAP_EVAL or build Release)")

_, CM = load_device()


def _layouts(n):
    rng = np.random.default_rng(12345)
    return {"identity": list(range(n)), "random": [int(x) for x in rng.permutation(N_PHYS)[:n]]}


CASES = [(name, kind) for name in ("qft_n8", "qaoa_n12", "qft_n16") for kind in ("identity", "random")]


@pytest.mark.parametrize("name,kind", CASES)
def test_cpp_router_output_validates(name, kind, tmp_path):
    original = load_instance(name)
    layout = _layouts(original.num_qubits)[kind]
    qasm_out, layout_out = tmp_path / "routed.qasm", tmp_path / "layout.json"
    proc = subprocess.run(
        [str(EVAL), "--instance", str(ROOT / "data" / "instances" / f"{name}.qasm"),
         "--device", str(DEVICE_JSON), "--layout", ",".join(map(str, layout)),
         "--out-qasm", str(qasm_out), "--out-layout", str(layout_out)],
        capture_output=True, text=True, check=True)
    reported = int(proc.stdout.strip().split("=")[1])

    side = json.loads(layout_out.read_text(encoding="utf-8"))
    assert side["initial"] == layout
    assert side["swaps"] == reported
    routed = loads(qasm_out.read_text(encoding="utf-8"))
    report = validate(original, routed, side["initial"], side["final"], CM, k=1)
    assert report["valid"], report
    assert report["swaps"] == reported
