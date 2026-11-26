"""Table functions of qmap.figures on the real data (no drawing, no C++ executable)."""
from qmap import figures


def test_instance_rows():
    df = figures.instance_rows()
    assert len(df) == 12
    assert set(df["type"]) == {"QFT", "QAOA"}
    assert (df["cx"] > 0).all()
    assert (df["gates"] == df["gates_1q"] + df["cx"]).all()
    assert df[df["type"] == "QAOA"]["seed"].notna().all()


def test_latex_table_and_device_summary():
    text = figures.latex_table(["A", "B"], [["x\\_y", 1], ["z", None]], "lr")
    assert "\\toprule" in text and "x\\_y & 1 \\\\" in text and "z & -- \\\\" in text
    s = figures.device_summary()
    assert (s["qubits"], s["edges"]) == (27, 28)
    assert sum(s["degree_histogram"].values()) == 27


def test_table_device_escapes_name(tmp_path):
    figures.table_device(tmp_path)
    text = (tmp_path / "device_falcon27.tex").read_text(encoding="utf-8")
    assert r"\texttt{fake\_kolkata}" in text
