from pytype.rewrite.abstract import abstract
from pytype.rewrite.tests import test_utils

import unittest


class SpecialBuiltinsTest(test_utils.ContextfulTestBase):

  def load_builtin_function(self, name: str) -> abstract.PytdFunction:
    func = self.ctx.abstract_loader.load_builtin(name)
    assert isinstance(func, abstract.PytdFunction)
    return func


class AssertTypeTest(SpecialBuiltinsTest):

  def test_types_match(self):
    assert_type_func = self.load_builtin_function('assert_type')
    var = self.ctx.consts[0].to_variable()
    typ = abstract.SimpleClass(self.ctx, 'int', {}).to_variable()
    ret = assert_type_func.call(abstract.Args(posargs=(var, typ)))
    self.assertEqual(ret.get_return_value(), self.ctx.consts[None])
    self.assertEqual(len(self.ctx.errorlog), 0)  # pylint: disable=g-generic-assert


class RevealTypeTest(SpecialBuiltinsTest):

  def test_basic(self):
    reveal_type_func = self.load_builtin_function('reveal_type')
    var = self.ctx.consts[0].to_variable()
    ret = reveal_type_func.call(abstract.Args(posargs=(var,)))
    self.assertEqual(ret.get_return_value(), self.ctx.consts[None])
    self.assertEqual(len(self.ctx.errorlog), 1)


if __name__ == '__main__':
  unittest.main()
