"""Four one-bit full adders connected by carries. Python 3, no build needed."""
import argparse


def full_adder(a, b, carry):
    return a ^ b ^ carry, (a & b) | ((a ^ b) & carry)


def add4(a, b):
    carry = result = 0
    for bit in range(4):  # low bit first: its carry feeds the next stage
        x, y = (a >> bit) & 1, (b >> bit) & 1
        digit, next_carry = full_adder(x, y, carry)
        print(f"bit {bit}: {x}+{y}+carry {carry} -> sum {digit}, carry {next_carry}")
        result |= digit << bit
        carry = next_carry
    return result, carry


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("a", type=int, choices=range(16))
    parser.add_argument("b", type=int, choices=range(16))
    args = parser.parse_args()
    result, carry = add4(args.a, args.b)
    print(f"low four bits: {result:04b} ({result}), carry out: {carry}")
