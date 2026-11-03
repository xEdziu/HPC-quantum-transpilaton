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
from qiskit import QuantumCircuit
from qiskit.converters import circuit_to_dag
from qiskit.transpiler import PassManager
from qiskit.transpiler.passes import CheckMap


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
