import dataclasses
from typing import Any

import immutabledict
from pytype.rewrite.flow import conditions
from pytype.rewrite.flow import state
from pytype.rewrite.flow import variables
from typing_extensions import assert_type

import unittest


@dataclasses.dataclass(frozen=True)
class FakeCondition(conditions.Condition):
  name: str


class LocalsTest(unittest.TestCase):

  def test_store_local(self):
    b = state.BlockState({})
    var = variables.Variable.from_value(42)
    b.store_local('x', var)
    self.assertEqual(b._locals, {'x': var})

  def test_load_local(self):
    b = state.BlockState({})
    b.store_local('x', variables.Variable.from_value(42))
    x = b.load_local('x')
    self.assertEqual(x.name, 'x')
    self.assertEqual(x.get_atomic_value(), 42)

  def test_get_locals(self):
    x = variables.Variable.from_value(42)
    b = state.BlockState({'x': x})
    self.assertEqual(b.get_locals(), immutabledict.immutabledict({'x': x}))

  def test_typing(self):
    locals_ = {'x': variables.Variable.from_value(0)}
    b = state.BlockState(locals_)
    assert_type(b, state.BlockState[int])
    assert_type(b.load_local('x'), variables.Variable[int])


class WithConditionTest(unittest.TestCase):

  def test_add_condition_to_true(self):
    b = state.BlockState({})
    var = variables.Variable.from_value(42)
    condition = FakeCondition('a')

    b.store_local('x', var)
    b2 = b.with_condition(condition)

    self.assertEqual(b2._condition, condition)
    self.assertIs(b2._locals['x'], var)
    self.assertEqual(b2._locals_with_block_condition, {'x'})

  def test_add_condition_to_condition(self):
    b = state.BlockState({})
    condition1 = FakeCondition('a')
    condition2 = FakeCondition('b')
    b._condition = condition1
    var = variables.Variable.from_value(42)

    b.store_local('x', var)
    b2 = b.with_condition(condition2)

    self.assertEqual(b2._condition, conditions.And(condition1, condition2))
    self.assertIs(b2._locals['x'], var)
    self.assertEqual(b2._locals_with_block_condition, {'x'})

  def test_add_condition_no_block_condition(self):
    var = variables.Variable.from_value(42)
    b = state.BlockState({'x': var}, locals_with_block_condition=set())
    condition = FakeCondition('a')
    b2 = b.with_condition(condition)

    self.assertEqual(b2._condition, condition)
    x = b2._locals['x']
    self.assertEqual(len(x.bindings), 1)
    self.assertEqual(x.bindings[0], variables.Binding(42, condition))
    self.assertFalse(b2._locals_with_block_condition)


class MergeIntoTest(unittest.TestCase):

  def test_merge_into_none(self):
    b1 = state.BlockState({})
    b2 = b1.merge_into(None)
    self.assertIsNot(b1, b2)
    self.assertFalse(b2._locals)
    self.assertIs(b2._condition, conditions.TRUE)

  def test_merge_into_other(self):
    c1 = FakeCondition('a')
    c2 = FakeCondition('b')
    b1 = state.BlockState({}, c1)
    b2 = state.BlockState({}, c2)
    b3 = b1.merge_into(b2)
    self.assertFalse(b3._locals)
    self.assertEqual(b3._condition, conditions.Or(c1, c2))

  def test_same_variable(self):
    var = variables.Variable.from_value(42)
    condition1 = FakeCondition('a')
    condition2 = FakeCondition('b')
    b1 = state.BlockState({'x': var}, condition1)
    b2 = state.BlockState({'x': var}, condition2)
    b3 = b1.merge_into(b2)
    self.assertEqual(b3._locals, {'x': var})
    self.assertEqual(b3._condition, conditions.Or(condition1, condition2))
    self.assertEqual(b3._locals_with_block_condition, {'x'})

  def test_add_block_condition_self(self):
    condition = FakeCondition('a')
    b1 = state.BlockState({'x': variables.Variable.from_value(42)}, condition)
    b2 = state.BlockState({})
    b3 = b1.merge_into(b2)
    self.assertEqual(set(b3._locals), {'x'})
    x = b3._locals['x']
    self.assertEqual(len(x.bindings), 1)
    self.assertEqual(x.bindings[0], variables.Binding(42, condition))
    self.assertIs(b3._condition, conditions.TRUE)
    self.assertFalse(b3._locals_with_block_condition)

  def test_add_block_condition_other(self):
    condition = FakeCondition('a')
    b1 = state.BlockState({})
    b2 = state.BlockState({'x': variables.Variable.from_value(42)}, condition)
    b3 = b1.merge_into(b2)
    self.assertEqual(set(b3._locals), {'x'})
    x = b3._locals['x']
    self.assertEqual(len(x.bindings), 1)
    self.assertEqual(x.bindings[0], variables.Binding(42, condition))
    self.assertIs(b3._condition, conditions.TRUE)
    self.assertFalse(b3._locals_with_block_condition)

  def test_noadd_block_condition(self):
    condition1 = FakeCondition('a')
    var1 = variables.Variable.from_value(42)
    b1 = state.BlockState({'x': var1}, condition1,
                          locals_with_block_condition=set())
    condition2 = FakeCondition('b')
    var2 = variables.Variable.from_value(3.14)
    b2 = state.BlockState({'y': var2}, condition2,
                          locals_with_block_condition=set())
    b3 = b1.merge_into(b2)
    self.assertEqual(set(b3._locals), {'x', 'y'})
    x = b3._locals['x']
    y = b3._locals['y']
    self.assertIs(x, var1)
    self.assertIs(y, var2)

  def test_merge_bindings(self):
    var1 = variables.Variable.from_value(42)
    b1 = state.BlockState({'x': var1})
    var2 = variables.Variable.from_value(3.14)
    b2 = state.BlockState({'x': var2})
    b3 = b1.merge_into(b2)
    self.assertEqual(set(b3._locals), {'x'})
    x = b3._locals['x']
    self.assertCountEqual(
        x.bindings, [variables.Binding(42), variables.Binding(3.14)])

  def test_merge_conditions(self):
    condition1 = FakeCondition('a')
    condition2 = FakeCondition('b')
    b1 = state.BlockState(
        {'x': variables.Variable((variables.Binding(42, condition1),))},
        conditions.TRUE)
    b2 = state.BlockState(
        {'x': variables.Variable((variables.Binding(42, condition2),))},
        conditions.TRUE)
    b3 = b1.merge_into(b2)
    self.assertEqual(set(b3._locals), {'x'})
    x = b3._locals['x']
    self.assertEqual(len(x.bindings), 1)
    self.assertEqual(x.bindings[0].value, 42)
    self.assertEqual(x.bindings[0].condition,
                     conditions.Or(condition1, condition2))
    self.assertEqual(b3._condition, conditions.TRUE)


class FlowTest(unittest.TestCase):

  def test_diamond(self):
    #      start_state
    #          / \
    # jump_state  nojump_state
    #          \ /
    #      join_state
    #
    # This test simulates:
    #   x = 42  # start_state
    #   if not a:
    #     y = None  # nojump_state
    #   else:
    #     y = 3.14  # jump_state
    #   ...  # join_state
    var = variables.Variable.from_value(42)
    start_state = state.BlockState[Any]({'x': var})

    jump_condition = FakeCondition('a')
    nojump_condition = conditions.Not(jump_condition)

    jump_state = start_state.with_condition(jump_condition)
    jump_state.store_local('y', variables.Variable.from_value(3.14))
    nojump_state = start_state.with_condition(nojump_condition)
    nojump_state.store_local('y', variables.Variable.from_value(None))

    join_state = jump_state.merge_into(nojump_state.merge_into(None))
    self.assertEqual(set(join_state._locals), {'x', 'y'})
    x = join_state._locals['x']
    y = join_state._locals['y']
    self.assertEqual(x, var)
    self.assertCountEqual(
        y.bindings, [variables.Binding(3.14, jump_condition),
                     variables.Binding(None, nojump_condition)])
    self.assertIs(join_state._condition, conditions.TRUE)
    self.assertEqual(join_state._locals_with_block_condition, {'x'})

  def test_nested_jumps(self):
    #                   start_state
    #                       / \
    #            nojump_state  jump_state
    #                 / \
    # nested_jump_state  nested_nojump_state
    #                 \ /
    #          nested_join_state
    #
    # This test simulates:
    #   x = 42  # start_state
    #   if a:  # nojump_state
    #     if b:
    #       y = 3.14  # nested_nojump_state
    #     else:
    #       y = None  # nested_jump_state
    #     ...  # nested_join_state
    #   else: ...  # jump_state
    var = variables.Variable.from_value(42)
    start_state = state.BlockState[Any]({'x': var})

    nojump_condition = FakeCondition('a')
    nojump_state = start_state.with_condition(nojump_condition)

    nested_nojump_condition = FakeCondition('b')
    nested_jump_condition = conditions.Not(nested_nojump_condition)
    nested_nojump_state = nojump_state.with_condition(nested_nojump_condition)
    nested_nojump_state.store_local('y', variables.Variable.from_value(3.14))
    nested_jump_state = nojump_state.with_condition(nested_jump_condition)
    nested_jump_state.store_local('y', variables.Variable.from_value(None))

    join_state = nested_jump_state.merge_into(
        nested_nojump_state.merge_into(None))
    self.assertEqual(set(join_state._locals), {'x', 'y'})
    x = join_state._locals['x']
    y = join_state._locals['y']
    self.assertEqual(x, var)
    self.assertCountEqual(
        y.bindings,
        [variables.Binding(3.14, conditions.And(nojump_condition,
                                                nested_nojump_condition)),
         variables.Binding(None, conditions.And(nojump_condition,
                                                nested_jump_condition))])
    # This is '(a and b) or (a and not b)', which ought to simplify to
    # 'a and (b or not b)', then to 'a', but conditions isn't that smart yet.
    self.assertEqual(
        join_state._condition,
        conditions.Or(
            conditions.And(nojump_condition, nested_nojump_condition),
            conditions.And(nojump_condition, nested_jump_condition)))
    self.assertEqual(join_state._locals_with_block_condition, {'x'})


if __name__ == '__main__':
  unittest.main()
