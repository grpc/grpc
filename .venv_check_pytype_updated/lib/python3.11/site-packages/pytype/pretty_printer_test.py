"""Test pretty_printer.py."""

from pytype import config
from pytype import pretty_printer
from pytype.pytd import pytd
from pytype.tests import test_base
from pytype.tests import test_utils

import unittest


class PrettyPrinterTest(test_base.UnitTest):

  def setUp(self):
    super().setUp()
    options = config.Options.create(python_version=self.python_version)
    self._ctx = test_utils.make_context(options)

  def test_constant_printer(self):
    pp = pretty_printer.PrettyPrinter(self._ctx)
    pyval = (1, 2, pytd.AnythingType(), 4)
    const = self._ctx.convert.constant_to_value(pyval)
    ret = pp.show_constant(const)
    self.assertEqual(ret, "(1, 2, ..., 4)")


if __name__ == "__main__":
  unittest.main()
