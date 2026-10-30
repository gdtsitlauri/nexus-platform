"""Executable semantics of guarded writes; a teaching model, not a CPU timing model.

Run: python examples/basics/predication.py
A false predicate preserves the destination and does not evaluate its expression.
Python's if implements that rule here; this does not imply branchless native code.
"""


def write_if(registers, destination, predicate, expression, trace=False):
    if predicate:
        registers[destination] = expression()
        action = "write"
    else:
        action = "keep"  # expression() must not run on this path
    if trace:
        print(f"  predicate={int(predicate)}: {action} {destination}={registers[destination]}")


def maximum(a, b, trace=False):
    registers = {"x": 0}
    predicate = a < b
    write_if(registers, "x", predicate, lambda: b, trace)
    write_if(registers, "x", not predicate, lambda: a, trace)
    return registers["x"]


def guarded_divide(numerator, denominator, trace=False):
    registers = {"q": 99}  # a visible old value, retained if the predicate is false
    write_if(registers, "q", denominator != 0, lambda: numerator // denominator, trace)
    return registers["q"]


if __name__ == "__main__":
    for a, b in ((3, 7), (7, 3)):
        print(f"max({a}, {b}):")
        print(f"  result={maximum(a, b, trace=True)}")
    for denominator in (3, 0):
        print(f"guarded 12 // {denominator}:")
        print(f"  result={guarded_divide(12, denominator, trace=True)}")
    try:
        # Selecting a value AFTER evaluating both expressions is a different operation.
        dangerous = 12 // 0
    except ZeroDivisionError:
        print("eager division: ZeroDivisionError (discarding its result would be too late)")
