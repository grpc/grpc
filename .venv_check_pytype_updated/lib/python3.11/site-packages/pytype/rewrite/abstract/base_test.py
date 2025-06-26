from pytype.rewrite.abstract import base
from pytype.rewrite.abstract import classes
from pytype.rewrite.flow import variables
from pytype.rewrite.tests import test_utils
from typing_extensions import assert_type

import unittest


class FakeValue(base.BaseValue):

  def __repr__(self):
    return 'FakeValue'

  @property
  def _attrs(self):
    return (id(self),)


class TestBase(test_utils.ContextfulTestBase):

  def _const(self, const):
    return base.PythonConstant(self.ctx, const, allow_direct_instantiation=True)


class BaseValueTest(TestBase):

  def test_to_variable(self):
    v = FakeValue(self.ctx)
    var = v.to_variable()
    assert_type(var, variables.Variable[FakeValue])
    self.assertEqual(var.get_atomic_value(), v)
    self.assertIsNone(var.name)

  def test_name(self):
    var = FakeValue(self.ctx).to_variable('NamedVariable')
    self.assertEqual(var.name, 'NamedVariable')


class PythonConstantTest(TestBase):

  def test_equal(self):
    c1 = self._const('a')
    c2 = self._const('a')
    self.assertEqual(c1, c2)

  def test_not_equal(self):
    c1 = self._const('a')
    c2 = self._const('b')
    self.assertNotEqual(c1, c2)

  def test_constant_type(self):
    c = self._const('a')
    assert_type(c.constant, str)

  def test_get_type_from_variable(self):
    var = self._const(True).to_variable()
    const = var.get_atomic_value(base.PythonConstant[int]).constant
    assert_type(const, int)

  def test_direct_instantiation(self):
    with self.assertRaises(ValueError):
      base.PythonConstant(self.ctx, None)


class SingletonTest(TestBase):

  def test_direct_instantiation(self):
    with self.assertRaises(ValueError):
      base.Singleton(self.ctx, 'TEST_SINGLETON')


class UnionTest(TestBase):

  def test_basic(self):
    options = (self._const(True), self._const(False))
    union = base.Union(self.ctx, options)
    self.assertEqual(union.options, options)

  def test_flatten(self):
    union1 = base.Union(self.ctx, (self._const(True), self._const(False)))
    union2 = base.Union(self.ctx, (union1, self._const(5)))
    self.assertEqual(union2.options,
                     (self._const(True), self._const(False), self._const(5)))

  def test_deduplicate(self):
    true = self._const(True)
    false = self._const(False)
    union = base.Union(self.ctx, (true, false, true))
    self.assertEqual(union.options, (true, false))

  def test_order(self):
    true = self._const(True)
    false = self._const(False)
    self.assertEqual(base.Union(self.ctx, (true, false)),
                     base.Union(self.ctx, (false, true)))

  def test_instantiate(self):
    class_x = classes.SimpleClass(self.ctx, 'X', {})
    class_y = classes.SimpleClass(self.ctx, 'Y', {})
    union = base.Union(self.ctx, (class_x, class_y))
    union_instance = union.instantiate()
    instance_x = classes.MutableInstance(self.ctx, class_x).freeze()
    instance_y = classes.MutableInstance(self.ctx, class_y).freeze()
    self.assertEqual(union_instance,
                     base.Union(self.ctx, (instance_x, instance_y)))


if __name__ == '__main__':
  unittest.main()
