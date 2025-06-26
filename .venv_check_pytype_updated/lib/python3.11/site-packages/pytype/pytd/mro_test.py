import textwrap

from pytype import config
from pytype import load_pytd
from pytype.pyi import parser
from pytype.pytd import mro
from pytype.pytd import visitors
from pytype.pytd.parse import parser_test_base

import unittest


class MroTest(parser_test_base.ParserTest):
  """Test pytype.pytd.mro."""

  def test_dedup(self):
    self.assertEqual([], mro.Dedup([]))
    self.assertEqual([1], mro.Dedup([1]))
    self.assertEqual([1, 2], mro.Dedup([1, 2]))
    self.assertEqual([1, 2], mro.Dedup([1, 2, 1]))
    self.assertEqual([1, 2], mro.Dedup([1, 1, 2, 2]))
    self.assertEqual([3, 2, 1], mro.Dedup([3, 2, 1, 3]))

  def test_mro_merge(self):
    self.assertEqual([], mro.MROMerge([[], []]))
    self.assertEqual([1], mro.MROMerge([[], [1]]))
    self.assertEqual([1], mro.MROMerge([[1], []]))
    self.assertEqual([1, 2], mro.MROMerge([[1], [2]]))
    self.assertEqual([1, 2], mro.MROMerge([[1, 2], [2]]))
    self.assertEqual([1, 2, 3, 4], mro.MROMerge([[1, 2, 3], [2, 4]]))
    self.assertEqual([1, 2, 3], mro.MROMerge([[1, 2], [1, 2, 3]]))
    self.assertEqual([1, 2], mro.MROMerge([[1, 1], [2, 2]]))
    self.assertEqual(
        [1, 2, 3, 4, 5, 6], mro.MROMerge([[1, 3, 5], [2, 3, 4], [4, 5, 6]])
    )
    self.assertEqual([1, 2, 3], mro.MROMerge([[1, 2, 1], [2, 3, 2]]))

  def test_get_bases_in_mro(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      from typing import Generic, TypeVar
      T = TypeVar("T")
      class Foo(Generic[T]): pass
      class Bar(Foo[int]): pass
    """),
        options=self.options,
    )
    ast = ast.Visit(visitors.AdjustTypeParameters())
    loader = load_pytd.Loader(
        config.Options.create(python_version=self.python_version)
    )
    ast = loader.resolve_ast(ast)
    bases = mro.GetBasesInMRO(ast.Lookup("Bar"), lookup_ast=ast)
    self.assertListEqual(
        ["Foo", "typing.Generic", "builtins.object"], [t.name for t in bases]
    )


if __name__ == "__main__":
  unittest.main()
