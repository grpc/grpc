"""Base code for pyi parsing tests."""

import re
import textwrap

from pytype.pyi import parser
from pytype.pytd import pytd_utils
from pytype.tests import test_base

IGNORE = object()


class ParserTestBase(test_base.UnitTest):
  """Base class for pyi parsing tests."""

  def setUp(self):
    super().setUp()
    self.options = parser.PyiOptions(python_version=self.python_version)

  def parse(self, src, name=None, version=None, platform="linux"):
    if version:
      self.options.python_version = version
    self.options.platform = platform
    version = version or self.python_version
    src = textwrap.dedent(src).lstrip()
    ast = parser.parse_string(src, name=name, options=self.options)
    return ast

  def check(
      self,
      src,
      expected=None,
      prologue=None,
      name=None,
      version=None,
      platform="linux",
  ):
    """Check the parsing of src.

    This checks that parsing the source and then printing the resulting
    AST results in the expected text.

    Args:
      src: A source string.
      expected: Optional expected result string.  If not provided, src is used
        instead.  The special value IGNORE can be used to skip checking the
        parsed results against expected text.
      prologue: An optional prologue to be prepended to the expected text before
        comparison.  Useful for imports that are introduced during printing the
        AST.
      name: The name of the module.
      version: A python version tuple (None for default value).
      platform: A platform string (defaults to "linux").

    Returns:
      The parsed pytd.TypeDeclUnit.
    """
    ast = self.parse(src, name, version, platform)
    actual = pytd_utils.Print(ast)
    if expected != IGNORE:
      if expected is None:
        expected = src
      expected = textwrap.dedent(expected).lstrip()
      if prologue:
        expected = f"{textwrap.dedent(prologue)}\n\n{expected}"
      # Allow blank lines at the end of `expected` for prettier tests.
      self.assertMultiLineEqual(expected.rstrip(), actual)
    return ast

  def check_error(self, src, expected_line, message):
    """Check that parsing the src raises the expected error."""
    with self.assertRaises(parser.ParseError) as e:
      parser.parse_string(textwrap.dedent(src).lstrip(), options=self.options)
    self.assertRegex(str(e.exception), re.escape(message))
    self.assertEqual(expected_line, e.exception.line)
