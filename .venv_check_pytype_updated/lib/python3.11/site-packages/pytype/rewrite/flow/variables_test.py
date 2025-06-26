from typing import Union

from pytype.rewrite.flow import conditions
from pytype.rewrite.flow import variables
from typing_extensions import assert_type

import unittest


class BindingTest(unittest.TestCase):

  def test_type(self):
    b = variables.Binding(0)
    assert_type(b, variables.Binding[int])


class VariableTest(unittest.TestCase):

  def test_from_value(self):
    var = variables.Variable.from_value(0)
    assert_type(var, variables.Variable[int])
    self.assertIsNone(var.name)

  def test_from_value_with_name(self):
    var = variables.Variable.from_value(0, name='Zero')
    self.assertEqual(var.name, 'Zero')

  def test_multiple_bindings(self):
    var = variables.Variable((variables.Binding(0), variables.Binding('')))
    assert_type(var, variables.Variable[Union[int, str]])

  def test_multiple_bindings_superclass(self):
    class Parent:
      pass

    class Child1(Parent):
      pass

    class Child2(Parent):
      pass
    var: variables.Variable[Parent] = variables.Variable(
        (variables.Binding(Child1()), variables.Binding(Child2())))
    assert_type(var, variables.Variable[Parent])

  def test_get_atomic_value(self):
    var = variables.Variable.from_value(0)
    val = var.get_atomic_value()
    assert_type(val, int)
    self.assertEqual(val, 0)

  def test_get_atomic_value_empty_variable(self):
    var = variables.Variable(())
    with self.assertRaisesRegex(ValueError, 'Too few bindings'):
      var.get_atomic_value()

  def test_get_atomic_value_with_type(self):
    var: variables.Variable[int] = variables.Variable.from_value(True)
    val = var.get_atomic_value(bool)
    assert_type(var, variables.Variable[int])
    assert_type(val, bool)
    self.assertEqual(val, True)

  def test_get_atomic_value_with_wrong_type(self):
    var = variables.Variable.from_value(0)
    with self.assertRaisesRegex(ValueError, 'expected str, got int'):
      var.get_atomic_value(str)

  def test_get_atomic_value_with_parameterized_type(self):
    var = variables.Variable.from_value([0])
    val = var.get_atomic_value(list[int])
    assert_type(val, list[int])
    self.assertEqual(val, [0])

  def test_get_atomic_value_with_wrong_parameterized_type(self):
    var = variables.Variable.from_value(0)
    with self.assertRaisesRegex(ValueError, 'expected list, got int'):
      var.get_atomic_value(list[int])

  def test_get_atomic_value_multiple_bindings(self):
    var = variables.Variable((variables.Binding(0), variables.Binding('')))
    with self.assertRaisesRegex(ValueError, 'Too many bindings'):
      var.get_atomic_value()

  def test_with_true_condition(self):
    var = variables.Variable.from_value(0)
    var2 = var.with_condition(conditions.TRUE)
    self.assertIs(var2, var)

  def test_with_condition(self):
    var = variables.Variable.from_value(0)
    var2 = var.with_condition(conditions.FALSE)
    self.assertEqual(len(var2.bindings), 1)
    self.assertEqual(var2.bindings[0].value, 0)
    self.assertIs(var2.bindings[0].condition, conditions.FALSE)

  def test_values(self):
    var = variables.Variable.from_value(0)
    values = var.values
    assert_type(values, tuple[int, ...])
    self.assertEqual(values, (0,))

  def test_with_name(self):
    var = variables.Variable.from_value(0)
    x = var.with_name('x')
    self.assertEqual(x.name, 'x')
    self.assertEqual(x.values, (0,))

  def test_with_no_name(self):
    x = variables.Variable(name='x', bindings=(variables.Binding(0),))
    var = x.with_name(None)
    self.assertIsNone(var.name)
    self.assertEqual(var.values, (0,))

  def test_has_atomic_value(self):
    x = variables.Variable.from_value(5)
    self.assertTrue(x.has_atomic_value(5))
    self.assertFalse(x.has_atomic_value(6))

  def test_multiple_bindings_does_not_have_atomic_value(self):
    x = variables.Variable(
        bindings=(variables.Binding(5), variables.Binding(6)))
    self.assertFalse(x.has_atomic_value(5))
    self.assertFalse(x.has_atomic_value(6))

  def test_empty_does_not_have_atomic_value(self):
    x = variables.Variable(bindings=())
    self.assertFalse(x.has_atomic_value(5))

  def test_with_value(self):
    x = variables.Variable.from_value(5)
    y = x.with_value('test')
    assert_type(y, variables.Variable[str])
    self.assertEqual(y.get_atomic_value(), 'test')


if __name__ == '__main__':
  unittest.main()
