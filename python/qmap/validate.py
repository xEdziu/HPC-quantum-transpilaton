"""Validation of routed circuits against the original logical circuit.

Layout convention (shared by the C++ router and the SABRE baseline): `initial` and
`final` are lists of length n (logical qubits); entry i is the physical qubit holding
logical qubit i at the start / end of the routed circuit. The routed circuit has
n_phys qubits and contains only {rz, sx, x, cx} plus `swap`.

Checks:
  1. check_map: every 2q gate acts on a coupling-map edge (Qiskit CheckMap).
  2. check_structural: removing swaps and mapping physical back to logical must
     reproduce the original DAG, and the derived final layout must match.
  3. check_statevector: global equivalence on the full n_phys-qubit state. With a
     random continuous entangling prep V on the initial layout, the routed circuit R
     and the inverse of (original C after V) placed on the final layout, the
     probability of |0...0> over ALL physical qubits is
     |<0| (C V)^dag_final R V_init |0>|^2. It equals 1 iff R acts as C up to the layout
     permutation and a global phase on the ancilla-|0> subspace, with probability 1
     over the draw of V (a fixed V could be blind to some errors). Requiring the
     all-zero projection on the ancillas as well checks that they are returned to |0>.
"""
import argparse
import json
import sys

import numpy as np
from qiskit import QuantumCircuit
from qiskit.converters import circuit_to_dag
from qiskit.transpiler import PassManager
from qiskit.transpiler.passes import CheckMap
from qiskit_aer import AerSimulator
import qiskit_aer.library  # noqa: F401  (adds save_amplitudes_squared to QuantumCircuit)

from qmap.device import load_device
from qmap.instances import load_instance, loads


def check_map(routed, coupling_map):
    pm = PassManager([CheckMap(coupling_map)])
    pm.run(routed)
    return bool(pm.property_set["is_swap_mapped"])


def unroute(routed, initial, n):
    """Drop swaps and rename physical to logical qubits; returns (circuit, final)."""
    phys_to_log = {p: i for i, p in enumerate(initial)}  # missing key = ancilla
    logical = QuantumCircuit(n)
    for inst in routed.data:
        phys = [routed.find_bit(q).index for q in inst.qubits]
        if inst.operation.name == "swap":
            a, b = phys
            la, lb = phys_to_log.pop(a, None), phys_to_log.pop(b, None)
            if lb is not None:
                phys_to_log[a] = lb
            if la is not None:
                phys_to_log[b] = la
            continue
        logs = [phys_to_log.get(p) for p in phys]
        if None in logs:
            raise ValueError(f"gate {inst.operation.name} acts on an ancilla")
        logical.append(inst.operation, logs)
    final = [None] * n
    for p, l in phys_to_log.items():
        final[l] = p
    return logical, final


def check_structural(original, routed, initial, final):
    try:
        logical, derived = unroute(routed, initial, original.num_qubits)
    except ValueError:
        return False
    # Rebuild on the original's registers so DAG equality compares only structure.
    same = original.copy_empty_like()
    for inst in logical.data:
        same.append(inst.operation, [original.qubits[logical.find_bit(q).index] for q in inst.qubits])
    return derived == list(final) and circuit_to_dag(same) == circuit_to_dag(original)


def _random_prep(n, rng):
    """Random single-qubit U layer, CX on pairs (i, i+1) for even i, another U layer."""
    v = QuantumCircuit(n)
    for layer in range(2):
        for q, (t, p, l) in enumerate(rng.uniform(0, 2 * np.pi, (n, 3))):
            v.u(t, p, l, q)
        if layer == 0:
            for q in range(0, n - 1, 2):
                v.cx(q, q + 1)
    return v


def statevector_fidelity(original, routed, initial, final, seed):
    n, n_phys = original.num_qubits, routed.num_qubits
    v = _random_prep(n, np.random.default_rng(seed))
    qc = QuantumCircuit(n_phys)
    qc.compose(v, qubits=list(initial), inplace=True)
    qc.compose(routed, inplace=True)
    qc.compose(v.compose(original).inverse(), qubits=list(final), inplace=True)
    qc.save_amplitudes_squared([0], label="p0")
    sim = AerSimulator(method="statevector", precision="double")
    return float(sim.run(qc).result().data()["p0"][0])


def check_statevector(original, routed, initial, final, k=3, tol=1e-8, seed=0):
    fids = [statevector_fidelity(original, routed, initial, final, seed + j) for j in range(k)]
    return all(f >= 1 - tol for f in fids), fids


def validate(original, routed, initial, final, coupling_map, k=3):
    report = {
        "check_map": check_map(routed, coupling_map),
        "structural": check_structural(original, routed, initial, final),
    }
    report["statevector"], report["fidelities"] = check_statevector(original, routed, initial, final, k=k)
    report["swaps"] = routed.count_ops().get("swap", 0)
    report["valid"] = report["check_map"] and report["structural"] and report["statevector"]
    return report


def main():
    ap = argparse.ArgumentParser(description="Validate a routed circuit.")
    ap.add_argument("--instance", required=True)
    ap.add_argument("--routed", required=True)
    ap.add_argument("--layout", required=True)
    ap.add_argument("--k", type=int, default=3)
    args = ap.parse_args()
    original = load_instance(args.instance)
    with open(args.routed, encoding="utf-8") as f:
        routed = loads(f.read())
    with open(args.layout, encoding="utf-8") as f:
        layout = json.load(f)
    _, cm = load_device()
    report = validate(original, routed, layout["initial"], layout["final"], cm, k=args.k)
    print(json.dumps(report, indent=2))
    sys.exit(0 if report["valid"] else 1)


if __name__ == "__main__":
    main()
