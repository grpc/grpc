"""Test for binary operators."""

from pytype.rewrite import operators
from pytype.rewrite.flow import variables
from pytype.rewrite.tests import test_utils

import unittest


class BinaryOperatorTest(test_utils.ContextfulTestBase):

  def test_call_binary(self):
    a = self.ctx.consts[1].to_variable()
    b = self.ctx.consts[2].to_variable()
    ret = operators.call_binary(self.ctx, '__add__', a, b)
    self.assertIsInstance(ret, variables.Variable)


if __name__ == '__main__':
  unittest.main()
