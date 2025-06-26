"""Tests for pytype.pyi.evaluator."""

import ast as astlib

from pytype.pyi import evaluator
from pytype.pyi import types

import unittest

_eval = evaluator.eval_string_literal


class EvaluatorTest(unittest.TestCase):

  def test_str(self):
    self.assertEqual(_eval('"hello world"'), 'hello world')

  def test_num(self):
    self.assertEqual(_eval('3'), 3)

  def test_tuple(self):
    self.assertEqual(_eval('(None,)'), (None,))

  def test_list(self):
    self.assertEqual(_eval('[None]'), [None])

  def test_set(self):
    self.assertEqual(_eval('{None}'), {None})

  def test_dict(self):
    self.assertEqual(_eval('{"k": 0}'), {'k': 0})

  def test_name_constant(self):
    self.assertEqual(_eval('True'), True)

  def test_name(self):
    self.assertEqual(_eval('x'), 'x')

  def test_unop(self):
    self.assertEqual(_eval('-3'), -3)

  def test_binop(self):
    self.assertEqual(_eval('5 + 5'), 10)

  def test_constant(self):
    const = astlib.Constant('salutations')
    self.assertEqual(evaluator.literal_eval(const), 'salutations')

  def test_expr(self):
    expr = astlib.Expr(astlib.Constant(8))
    self.assertEqual(evaluator.literal_eval(expr), 8)

  def test_pyi_int_constant(self):
    const = types.Pyval.from_const(astlib.parse('42', mode='eval').body)
    self.assertEqual(evaluator.literal_eval(const), 42)

  def test_pyi_none_constant(self):
    const = types.Pyval.from_const(astlib.parse('None', mode='eval').body)
    self.assertIsNone(evaluator.literal_eval(const))


if __name__ == '__main__':
  unittest.main()
