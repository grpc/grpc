"""Utility classes for testing the PYTD parser."""

import os
import sys
import textwrap

from pytype import config
from pytype import load_pytd
from pytype.pyi import parser
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.tests import test_base


class ParserTest(test_base.UnitTest):
  """Test utility class. Knows how to parse PYTD and compare source code."""

  loader: load_pytd.Loader

  @classmethod
  def setUpClass(cls):
    super().setUpClass()
    cls.loader = load_pytd.Loader(
        config.Options.create(python_version=cls.python_version))

  def setUp(self):
    super().setUp()
    self.options = parser.PyiOptions(python_version=self.python_version)

  def Parse(self, src, name=None, version=None, platform=None):
    if version:
      self.options.python_version = version
    if platform:
      self.options.platform = platform
    tree = parser.parse_string(
        textwrap.dedent(src), name=name, options=self.options)
    tree = tree.Visit(visitors.NamedTypeToClassType())
    tree = tree.Visit(visitors.AdjustTypeParameters())
    # Convert back to named types for easier testing
    tree = tree.Visit(visitors.ClassTypeToNamedType())
    tree.Visit(visitors.VerifyVisitor())
    return tree

  def ParseWithBuiltins(self, src):
    ast = parser.parse_string(textwrap.dedent(src), options=self.options)
    ast = ast.Visit(visitors.LookupExternalTypes(
        {"builtins": self.loader.builtins, "typing": self.loader.typing}))
    ast = ast.Visit(visitors.NamedTypeToClassType())
    ast = ast.Visit(visitors.AdjustTypeParameters())
    ast.Visit(visitors.FillInLocalPointers({
        "": ast, "builtins": self.loader.builtins}))
    ast.Visit(visitors.VerifyVisitor())
    return ast

  def ToAST(self, src_or_tree):
    if isinstance(src_or_tree, str):
      # Put into a canonical form (removes comments, standard indents):
      return self.Parse(src_or_tree + "\n")
    else:  # isinstance(src_or_tree, tuple):
      src_or_tree.Visit(visitors.VerifyVisitor())
      return src_or_tree

  def AssertSourceEquals(self, src_or_tree_1, src_or_tree_2):
    # Strip leading "\n"s for convenience
    ast1 = self.ToAST(src_or_tree_1)
    ast2 = self.ToAST(src_or_tree_2)
    src1 = pytd_utils.Print(ast1).strip() + "\n"
    src2 = pytd_utils.Print(ast2).strip() + "\n"
    # Verify printed versions are the same and ASTs are the same.
    ast1 = ast1.Visit(visitors.ClassTypeToNamedType())
    ast2 = ast2.Visit(visitors.ClassTypeToNamedType())
    if src1 != src2 or not pytd_utils.ASTeq(ast1, ast2):
      # Due to differing opinions on the form of debug output, allow an
      # environment variable to control what output you want. Set
      # PY_UNITTEST_DIFF to get diff output.
      if os.getenv("PY_UNITTEST_DIFF"):
        self.maxDiff = None  # for better diff output (assertMultiLineEqual)  # pylint: disable=invalid-name
        self.assertMultiLineEqual(src1, src2)
      else:
        sys.stdout.flush()
        sys.stderr.flush()
        print("Source files or ASTs differ:", file=sys.stderr)
        print("-" * 36, " Actual ", "-" * 36, file=sys.stderr)
        print(textwrap.dedent(src1).strip(), file=sys.stderr)
        print("-" * 36, "Expected", "-" * 36, file=sys.stderr)
        print(textwrap.dedent(src2).strip(), file=sys.stderr)
        print("-" * 80, file=sys.stderr)
      if not pytd_utils.ASTeq(ast1, ast2):
        print("Actual AST:", ast1, file=sys.stderr)
        print("Expect AST:", ast2, file=sys.stderr)
      self.fail("source files differ")

  def ApplyVisitorToString(self, data, visitor):
    tree = self.Parse(data)
    new_tree = tree.Visit(visitor)
    return pytd_utils.Print(new_tree)
