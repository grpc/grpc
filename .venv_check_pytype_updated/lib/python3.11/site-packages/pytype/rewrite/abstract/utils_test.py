from pytype.rewrite.abstract import base
from pytype.rewrite.abstract import utils
from pytype.rewrite.tests import test_utils
from typing_extensions import assert_type

import unittest


class GetAtomicConstantTest(test_utils.ContextfulTestBase):

  def test_get(self):
    var = self.ctx.consts['a'].to_variable()
    const = utils.get_atomic_constant(var)
    self.assertEqual(const, 'a')

  def test_get_with_type(self):
    var = self.ctx.consts['a'].to_variable()
    const = utils.get_atomic_constant(var, str)
    assert_type(const, str)
    self.assertEqual(const, 'a')

  def test_get_with_bad_type(self):
    var = self.ctx.consts['a'].to_variable()
    with self.assertRaisesRegex(ValueError, 'expected int, got str'):
      utils.get_atomic_constant(var, int)

  def test_get_with_parameterized_type(self):
    var = self.ctx.consts[('a',)].to_variable()
    const = utils.get_atomic_constant(var, tuple[str, ...])
    assert_type(const, tuple[str, ...])
    self.assertEqual(const, ('a',))

  def test_get_with_bad_parameterized_type(self):
    var = self.ctx.consts['a'].to_variable()
    with self.assertRaisesRegex(ValueError, 'expected tuple, got str'):
      utils.get_atomic_constant(var, tuple[str, ...])


class JoinValuesTest(test_utils.ContextfulTestBase):

  def test_empty(self):
    self.assertEqual(utils.join_values(self.ctx, []), self.ctx.consts.Any)

  def test_one_value(self):
    a = self.ctx.consts['a']
    self.assertEqual(utils.join_values(self.ctx, [a]), a)

  def test_multiple_values(self):
    a = self.ctx.consts['a']
    b = self.ctx.consts['b']
    val = utils.join_values(self.ctx, [a, b])
    self.assertEqual(val, base.Union(self.ctx, (a, b)))


if __name__ == '__main__':
  unittest.main()
