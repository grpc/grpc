import dataclasses

from pytype.rewrite.flow import conditions

import unittest


@dataclasses.dataclass(frozen=True)
class FakeCondition(conditions.Condition):
  name: str


class ConditionTest(unittest.TestCase):

  def test_or_true(self):
    condition = conditions.Or(FakeCondition('a'), conditions.TRUE)
    self.assertIs(condition, conditions.TRUE)

  def test_or_false(self):
    c1 = FakeCondition('a')
    c2 = FakeCondition('b')
    or_condition = conditions.Or(c1, c2, conditions.FALSE)
    self.assertIsInstance(or_condition, conditions._Or)
    self.assertCountEqual(or_condition.conditions, (c1, c2))

  def test_or_singleton(self):
    condition = FakeCondition('a')
    or_condition = conditions.Or(condition, conditions.FALSE)
    self.assertIs(or_condition, condition)

  def test_and_false(self):
    condition = conditions.And(FakeCondition('a'), conditions.FALSE)
    self.assertIs(condition, conditions.FALSE)

  def test_and_true(self):
    c1 = FakeCondition('a')
    c2 = FakeCondition('b')
    and_condition = conditions.And(c1, c2, conditions.TRUE)
    self.assertIsInstance(and_condition, conditions._And)
    self.assertCountEqual(and_condition.conditions, (c1, c2))

  def test_and_singleton(self):
    condition = FakeCondition('a')
    and_condition = conditions.And(condition, conditions.TRUE)
    self.assertIs(and_condition, condition)

  def test_not_not(self):
    condition = FakeCondition('a')
    not_not_condition = conditions.Not(conditions.Not(condition))
    self.assertIs(not_not_condition, condition)

  def test_negation_in_and(self):
    condition = FakeCondition('a')
    not_condition = conditions.Not(condition)
    and_condition = conditions.And(condition, not_condition)
    self.assertIs(and_condition, conditions.FALSE)

  def test_negation_in_or(self):
    condition = FakeCondition('a')
    not_condition = conditions.Not(condition)
    or_condition = conditions.Or(condition, not_condition)
    self.assertIs(or_condition, conditions.TRUE)


if __name__ == '__main__':
  unittest.main()
