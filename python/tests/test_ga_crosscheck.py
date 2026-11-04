"""Cross-check: qmap_ga (GPU GA) output must pass the Python validator."""
import csv
import json
import os
import re
import subprocess
from pathlib import Path

import pytest

from qmap.device import load_device
from qmap.instances import load_instance, loads
from qmap.validate import validate

ROOT = Path(__file__).resolve().parents[2]
DEVICE_JSON = ROOT / "data" / "devices" / "falcon27.json"


def _find_ga():
    env = os.environ.get("QMAP_GA")
    path = Path(env) if env else ROOT / "build" / "Release" / "qmap_ga.exe"
    return path if path.is_file() else None


GA = _find_ga()
pytestmark = pytest.mark.skipif(GA is None, reason="qmap_ga not built (set QMAP_GA or build Release)")

_, CM = load_device()


@pytest.mark.parametrize("name", ["qft_n8", "qaoa_n12"])
def test_ga_output_validates(name, tmp_path):
    original = load_instance(name)
    csv_out, qasm_out, layout_out = tmp_path / "ga.csv", tmp_path / "routed.qasm", tmp_path / "layout.json"
    proc = subprocess.run(
        [str(GA), "--instance", str(ROOT / "data" / "instances" / f"{name}.qasm"),
         "--device", str(DEVICE_JSON), "--pop", "256", "--generations", "30", "--seed", "1",
         "--log", str(csv_out), "--out-qasm", str(qasm_out), "--out-layout", str(layout_out)],
        capture_output=True, text=True, check=True)
    summary = dict(re.findall(r"(\w+)=(\S+)", proc.stdout.strip().splitlines()[-1]))
    best = int(summary["best"])

    side = json.loads(layout_out.read_text(encoding="utf-8"))
    assert side["swaps"] == best
    routed = loads(qasm_out.read_text(encoding="utf-8"))
    report = validate(original, routed, side["initial"], side["final"], CM, k=1)
    assert report["valid"], report
    assert report["swaps"] == best

    rows = list(csv.DictReader(csv_out.read_text(encoding="utf-8").splitlines()))
    assert len(rows) == 31
    bests = [int(r["best"]) for r in rows]
    assert all(a >= b for a, b in zip(bests, bests[1:]))
    assert bests[-1] == best
