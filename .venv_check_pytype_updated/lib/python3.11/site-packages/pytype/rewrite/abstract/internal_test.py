from typing import Any

from pytype.rewrite.abstract import internal
from pytype.rewrite.tests import test_utils

import unittest


class FunctionArgDictTest(test_utils.ContextfulTestBase):

  def test_asserts_dict(self):
    _ = internal.FunctionArgDict(self.ctx, {
        'a': self.ctx.consts.Any.to_variable()
    })
    with self.assertRaises(AssertionError):
      x: Any = ['a', 'b']
      _ = internal.FunctionArgDict(self.ctx, x)

  def test_asserts_string_keys(self):
    with self.assertRaises(ValueError):
      x: Any = {1: 2}
      _ = internal.FunctionArgDict(self.ctx, x)


class SplatTest(test_utils.ContextfulTestBase):

  def test_basic(self):
    # Basic smoke test, remove when we have some real functionality to test.
    seq = self.ctx.types[tuple].instantiate()
    x = internal.Splat(self.ctx, seq)
    self.assertEqual(x.iterable, seq)


if __name__ == '__main__':
  unittest.main()
