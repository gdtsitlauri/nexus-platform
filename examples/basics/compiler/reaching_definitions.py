"""The diamond.nx CFG on paper: E -> T/F -> J, tracking definitions of y only.

Synchronous rounds make propagation visible; NEXUS's C++ solver updates in place,
so it may need fewer rounds to reach exactly the same fixed point.
"""

PREDS = {"E": (), "T": ("E",), "F": ("E",), "J": ("T", "F")}
GEN = {"E": {"d0"}, "T": {"d1"}, "F": {"d2"}, "J": set()}
DEFS = {"d0", "d1", "d2"}
KILL = {block: DEFS - gen if gen else set() for block, gen in GEN.items()}


def solve():
    out = {block: set() for block in PREDS}
    round_number = 0
    while True:
        print(f"round {round_number}: " + "  ".join(
            f"{block}={','.join(sorted(out[block])) or '-'}" for block in PREDS))
        updated = {}
        for block, predecessors in PREDS.items():
            incoming = set().union(*(out[p] for p in predecessors))
            updated[block] = GEN[block] | (incoming - KILL[block])
        if updated == out:
            return out
        out = updated
        round_number += 1


if __name__ == "__main__":
    solve()
