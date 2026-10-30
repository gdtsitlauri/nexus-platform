#!/usr/bin/env python3
"""Check semantic properties of the tiny predication and points-to explanations."""
import runpy
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREDICATION = runpy.run_path(str(ROOT / "examples/basics/predication.py"))
POINTS_TO = runpy.run_path(str(ROOT / "examples/basics/compiler/points_to.py"))


class PredicationTest(unittest.TestCase):
    def test_false_guard_has_no_effect_and_does_not_evaluate(self):
        registers = {"x": 17}

        def forbidden():
            self.fail("a false guard evaluated its expression")

        PREDICATION["write_if"](registers, "x", False, forbidden)
        self.assertEqual(registers, {"x": 17})

    def test_true_guard_evaluates_once(self):
        registers, effects = {"x": 17}, []

        def value():
            effects.append("evaluated")
            return 5

        PREDICATION["write_if"](registers, "x", True, value)
        self.assertEqual(registers, {"x": 5})
        self.assertEqual(effects, ["evaluated"])

    def test_results_for_both_predicates_and_equal_inputs(self):
        for a in (-7, 0, 3, 7):
            for b in (-7, 0, 3, 7):
                self.assertEqual(PREDICATION["maximum"](a, b), max(a, b))
        self.assertEqual(PREDICATION["guarded_divide"](12, 3), 4)
        self.assertEqual(PREDICATION["guarded_divide"](12, 0), 99)


class PointsToTest(unittest.TestCase):
    def test_indirect_store_and_load_reach_the_fixed_point(self):
        result = POINTS_TO["solve"](POINTS_TO["PROGRAM"])
        self.assertEqual(result["p"], {"x", "y"})
        self.assertEqual(result["pp"], {"p"})
        self.assertEqual(result["r"], {"x", "y"})
        self.assertEqual(result["s"], {"y"})

    def test_removing_store_removes_the_alias_opportunity(self):
        result = POINTS_TO["solve"](POINTS_TO["PROGRAM"][:-1])
        self.assertEqual(result["r"], {"x"})
        self.assertFalse(POINTS_TO["may_alias"](result, "r", "q"))

    def test_constraint_order_and_duplicates_do_not_change_solution(self):
        program = POINTS_TO["PROGRAM"]
        expected = POINTS_TO["solve"](program)
        self.assertEqual(POINTS_TO["solve"](reversed(program)), expected)
        self.assertEqual(POINTS_TO["solve"](program + program), expected)

    def test_copy_cycle_propagates_one_address_to_all_members(self):
        program = (("copy", "r", "q"), ("copy", "q", "p"),
                   ("copy", "p", "r"), ("address", "p", "x"))
        result = POINTS_TO["solve"](program)
        for name in ("p", "q", "r"):
            self.assertEqual(result[name], {"x"})


if __name__ == "__main__":
    unittest.main()
