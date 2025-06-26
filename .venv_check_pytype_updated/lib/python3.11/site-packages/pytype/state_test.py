"""Test state.py."""

from pytype import compare
from pytype import state as frame_state
from pytype.typegraph import cfg

import unittest


def source_summary(binding, **varnames):
  """A simple deterministic listing of source variables."""
  clauses = []
  name_map = {b: name for name, b in varnames.items()}
  for origin in binding.origins:
    for sources in origin.source_sets:
      bindings = [f"{name_map[b]}={b.data}" for b in sources]
      clauses.append(" ".join(sorted(bindings)))
  return " | ".join(sorted(clauses))


class FakeValue:

  def __init__(self, name, true_compat, false_compat):
    self._name = name
    self.compatible = {True: true_compat, False: false_compat}

  def __str__(self):
    return self._name


ONLY_TRUE = FakeValue("T", True, False)
ONLY_FALSE = FakeValue("F", False, True)
AMBIGUOUS = FakeValue("?", True, True)


def fake_compatible_with(value, logical_value):
  return value.compatible[logical_value]


class FakeContext:
  """Supports weakref."""


CTX = FakeContext()


class DataStackTest(unittest.TestCase):
  """Test data stack access and manipulation."""

  def setUp(self):
    super().setUp()
    stack = (1, 2, 3, 4, 5, 6)
    self.state = frame_state.FrameState(stack, [], None, CTX, None, None)

  def test_push(self):
    state = self.state.push(7, 8)
    self.assertEqual(state.data_stack, (1, 2, 3, 4, 5, 6, 7, 8))

  def test_peek(self):
    self.assertEqual(self.state.peek(2), 5)

  def test_top(self):
    self.assertEqual(self.state.top(), 6)
    self.assertEqual(self.state.topn(2), (5, 6))

  def test_pop(self):
    state, val = self.state.pop()
    self.assertEqual(val, 6)
    self.assertEqual(state.data_stack, (1, 2, 3, 4, 5))
    state = state.pop_and_discard()
    self.assertEqual(state.data_stack, (1, 2, 3, 4))
    state, values = state.popn(3)
    self.assertEqual(values, (2, 3, 4))
    self.assertEqual(state.data_stack, (1,))

  def test_set(self):
    state = self.state.set_top(7)
    self.assertEqual(state.data_stack, (1, 2, 3, 4, 5, 7))
    state = state.set_second(6)
    self.assertEqual(state.data_stack, (1, 2, 3, 4, 6, 7))

  def test_rotate(self):
    state = self.state.rotn(3)
    self.assertEqual(state.data_stack, (1, 2, 3, 6, 4, 5))

  def test_swap(self):
    state = self.state.swap(4)
    self.assertEqual(state.data_stack, (1, 2, 6, 4, 5, 3))


class ConditionTestBase(unittest.TestCase):

  def setUp(self):
    super().setUp()
    self._program = cfg.Program()
    self._node = self._program.NewCFGNode("test")

  def check_binding(self, expected, binding, **varnames):
    self.assertEqual(len(binding.origins), 1)
    self.assertEqual(self._node, binding.origins[0].where)
    self.assertEqual(expected, source_summary(binding, **varnames))


class ConditionTest(ConditionTestBase):

  def new_binding(self, value=AMBIGUOUS):
    var = self._program.NewVariable()
    return var.AddBinding(value)

  def test_no_parent(self):
    x = self.new_binding()
    y = self.new_binding()
    z = self.new_binding()
    c = frame_state.Condition(self._node, [[x, y], [z]])
    self.check_binding("x=? y=? | z=?", c.binding, x=x, y=y, z=z)

  def test_parent_combination(self):
    p = self.new_binding()
    x = self.new_binding()
    y = self.new_binding()
    z = self.new_binding()
    c = frame_state.Condition(self._node, [[x, y], [z]])
    self.check_binding("x=? y=? | z=?", c.binding, p=p, x=x, y=y, z=z)


class RestrictConditionTest(ConditionTestBase):

  def setUp(self):
    super().setUp()
    self._old_compatible_with = compare.compatible_with
    compare.compatible_with = fake_compatible_with

  def tearDown(self):
    super().tearDown()
    compare.compatible_with = self._old_compatible_with

  def test_split(self):
    # Test that we split both sides and everything is passed through correctly.
    var = self._program.NewVariable()
    var.AddBinding(ONLY_TRUE)
    var.AddBinding(ONLY_FALSE)
    var.AddBinding(AMBIGUOUS)
    true_cond = frame_state.restrict_condition(self._node, var, True)
    false_cond = frame_state.restrict_condition(self._node, var, False)
    self.check_binding(
        "v0=T | v2=?",
        true_cond.binding,
        v0=var.bindings[0],
        v1=var.bindings[1],
        v2=var.bindings[2],
    )
    self.check_binding(
        "v1=F | v2=?",
        false_cond.binding,
        v0=var.bindings[0],
        v1=var.bindings[1],
        v2=var.bindings[2],
    )

  def test_no_bindings(self):
    var = self._program.NewVariable()
    false_cond = frame_state.restrict_condition(self._node, var, False)
    true_cond = frame_state.restrict_condition(self._node, var, True)
    self.assertIs(frame_state.UNSATISFIABLE, false_cond)
    self.assertIs(frame_state.UNSATISFIABLE, true_cond)

  def test_none_restricted(self):
    var = self._program.NewVariable()
    var.AddBinding(AMBIGUOUS)
    false_cond = frame_state.restrict_condition(self._node, var, False)
    true_cond = frame_state.restrict_condition(self._node, var, True)
    self.assertIsNone(false_cond)
    self.assertIsNone(true_cond)

  def test_all_restricted(self):
    var = self._program.NewVariable()
    var.AddBinding(ONLY_FALSE)
    c = frame_state.restrict_condition(self._node, var, True)
    self.assertIs(frame_state.UNSATISFIABLE, c)

  def test_some_restricted(self):
    var = self._program.NewVariable()
    x = var.AddBinding(AMBIGUOUS)  # Can be true or false.
    y = var.AddBinding(ONLY_FALSE)
    c = frame_state.restrict_condition(self._node, var, True)
    self.check_binding("x=?", c.binding, x=x, y=y)


if __name__ == "__main__":
  unittest.main()
