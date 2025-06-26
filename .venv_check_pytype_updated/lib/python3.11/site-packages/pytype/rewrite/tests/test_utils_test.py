"""Tests for test utilities."""

from pytype.pyc import opcodes
from pytype.rewrite.tests import test_utils

import unittest


class TestUtilsTest(unittest.TestCase):

  def test_assemble_block(self):
    block = """
      # line 1
      BUILD_SET 0
      LOAD_CONST 0
      # line 2
      LOAD_CONST 1
      SET_ADD 2
      # line 3
      RETURN_VALUE
    """
    expected = [
        opcodes.BUILD_SET(0, 1, 1, 0, 0, 0, None),
        opcodes.LOAD_CONST(1, 1, 1, 0, 0, 0, 1),
        opcodes.LOAD_CONST(2, 2, 2, 0, 0, 1, 2),
        opcodes.SET_ADD(3, 2, 2, 0, 0, 2, None),
        opcodes.RETURN_VALUE(4, 3)
    ]
    actual = test_utils.assemble_block(block)
    for a, e in zip(actual.order[0].code, expected):
      self.assertEqual(a.__class__, e.__class__)
      self.assertEqual(a.index, e.index)
      self.assertEqual(a.line, e.line)
      if isinstance(a, opcodes.OpcodeWithArg):
        self.assertEqual(a.arg, e.arg)  # pytype: disable=attribute-error


if __name__ == '__main__':
  unittest.main()
