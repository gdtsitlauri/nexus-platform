"""Small inclusion-based, flow-insensitive points-to analysis of explicit constraints.

Run: python examples/basics/compiler/points_to.py
This is a closed teaching language, not a parser or sound analyser for arbitrary C.
"""

# C-like statements: p=&x; q=&y; s=q; pp=&p; r=*pp; *pp=q;
# An object named p is the memory cell holding pointer p; x/y hold scalar data.
PROGRAM = (
    ("address", "p", "x"),
    ("address", "q", "y"),
    ("copy", "s", "q"),
    ("address", "pp", "p"),
    ("load", "r", "pp"),
    ("store", "pp", "q"),
)


def describe(points):
    return "  ".join(f"{name}={{{','.join(sorted(objects))}}}"
                     for name, objects in sorted(points.items()) if objects) or "empty"


def solve(program, trace=False):
    program = tuple(program)
    names = {name for _, destination, source in program for name in (destination, source)}
    points = {name: set() for name in names}

    def include(destination, objects):
        before = len(points[destination])
        points[destination].update(objects)
        return len(points[destination]) != before

    round_number = 0
    if trace:
        print(f"round 0: {describe(points)}")
    while True:
        changed = False
        for operation, destination, source in program:
            if operation == "address":        # p = &x
                changed |= include(destination, {source})
            elif operation == "copy":         # p = q
                changed |= include(destination, points[source])
            elif operation == "load":         # p = *q
                for cell in tuple(points[source]):
                    changed |= include(destination, points[cell])
            elif operation == "store":        # *p = q
                for cell in tuple(points[destination]):
                    changed |= include(cell, points[source])
            else:
                raise ValueError(f"unknown constraint: {operation}")
        if not changed:
            if trace:
                print("fixed point: another pass adds nothing")
            return points
        round_number += 1
        if trace:
            print(f"round {round_number}: {describe(points)}")


def may_alias(points, left, right):
    return bool(points[left] & points[right])


if __name__ == "__main__":
    result = solve(PROGRAM, trace=True)
    print(f"r and q may alias: {may_alias(result, 'r', 'q')}")
    without_store = solve(PROGRAM[:-1])
    print(f"without *pp=q, r and q may alias: {may_alias(without_store, 'r', 'q')}")
