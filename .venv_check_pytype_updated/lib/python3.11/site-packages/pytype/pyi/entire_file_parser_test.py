"""Entire-file parsing test."""

from pytype.imports import builtin_stubs
from pytype.pyi import parser_test_base

import unittest


class EntireFileTest(parser_test_base.ParserTestBase):

  def test_builtins(self):
    _, builtins = builtin_stubs.GetPredefinedFile("builtins", "builtins")
    self.check(builtins, expected=parser_test_base.IGNORE)


if __name__ == "__main__":
  unittest.main()
