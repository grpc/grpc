from pytype import config
from pytype import load_pytd
from pytype.pytd import pytd
from pytype.rewrite import analyze
from pytype.rewrite import context

import unittest


class CheckTypesTest(unittest.TestCase):

  def test_smoke(self):
    options = config.Options.create('<file>')
    loader = load_pytd.create_loader(options)
    analysis = analyze.check_types(src='', options=options, loader=loader)
    self.assertIsInstance(analysis.context, context.Context)
    self.assertIsNone(analysis.ast)
    self.assertIsNone(analysis.ast_deps)


class InferTypesTest(unittest.TestCase):

  def test_smoke(self):
    options = config.Options.create('<file>')
    loader = load_pytd.create_loader(options)
    analysis = analyze.infer_types(src='', options=options, loader=loader)
    self.assertIsInstance(analysis.context, context.Context)
    self.assertIsInstance(analysis.ast, pytd.TypeDeclUnit)
    self.assertIsInstance(analysis.ast_deps, pytd.TypeDeclUnit)


if __name__ == '__main__':
  unittest.main()
