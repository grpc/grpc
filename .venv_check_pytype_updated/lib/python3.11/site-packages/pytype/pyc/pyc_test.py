"""Tests for pyc.py."""

from pytype.pyc import compiler
from pytype.pyc import opcodes
from pytype.pyc import pyc
from pytype.tests import test_base
import unittest


class TestCompileError(unittest.TestCase):

  def test_error_matches_re(self):
    e = pyc.CompileError("some error (foo.py, line 123)")
    self.assertEqual("foo.py", e.filename)
    self.assertEqual(123, e.line)
    self.assertEqual("some error", e.error)

  def test_error_does_not_match_re(self):
    e = pyc.CompileError("some error in foo.py at line 123")
    self.assertIsNone(e.filename)
    self.assertEqual(1, e.line)
    self.assertEqual("some error in foo.py at line 123", e.error)


class TestPyc(test_base.UnitTest):
  """Tests for pyc.py."""

  def _compile(self, src, mode="exec"):
    exe = (["python" + ".".join(map(str, self.python_version))], [])
    pyc_data = compiler.compile_src_string_to_pyc_string(
        src,
        filename="test_input.py",
        python_version=self.python_version,
        python_exe=exe,
        mode=mode,
    )
    return pyc.parse_pyc_string(pyc_data)

  def test_compile(self):
    code = self._compile("foobar = 3")
    self.assertIn("foobar", code.co_names)
    self.assertEqual(self.python_version, code.python_version)

  def test_compile_utf8(self):
    src = 'foobar = "abc□def"'
    code = self._compile(src)
    self.assertIn("foobar", code.co_names)
    self.assertEqual(self.python_version, code.python_version)

  def test_erroneous_file(self):
    with self.assertRaises(pyc.CompileError) as ctx:
      self._compile("\nfoo ==== bar--")
    self.assertEqual("test_input.py", ctx.exception.filename)
    self.assertEqual(2, ctx.exception.line)
    self.assertEqual("invalid syntax", ctx.exception.error)

  def test_lineno(self):
    code = self._compile(
        "a = 1\n"  # line 1
        "\n"  # line 2
        "a = a + 1\n"  # line 3
    )
    self.assertIn("a", code.co_names)
    op_and_line = [(op.name, op.line) for op in opcodes.dis(code)]
    expected = [
        ("LOAD_CONST", 1),
        ("STORE_NAME", 1),
        ("LOAD_NAME", 3),
        ("LOAD_CONST", 3),
        ("BINARY_ADD", 3),
        ("STORE_NAME", 3),
        ("LOAD_CONST", 3),
        ("RETURN_VALUE", 3),
    ]
    if self.python_version >= (3, 11):
      expected = [("RESUME", 0)] + expected
      expected[5] = ("BINARY_OP", 3)  # this was BINARY_ADD in 3.10-
    if self.python_version >= (3, 12):
      expected = expected[:-2] + [("RETURN_CONST", 3)]
    self.assertEqual(expected, op_and_line)

  def test_mode(self):
    code = self._compile("foo", mode="eval")
    self.assertIn("foo", code.co_names)
    ops = [op.name for op in opcodes.dis(code)]
    expected = ["LOAD_NAME", "RETURN_VALUE"]
    if self.python_version >= (3, 11):
      expected = ["RESUME"] + expected
    self.assertEqual(expected, ops)

  def test_singlelineno(self):
    code = self._compile("a = 1\n")  # line 1
    self.assertIn("a", code.co_names)
    op_and_line = [(op.name, op.line) for op in opcodes.dis(code)]
    expected = [
        ("LOAD_CONST", 1),
        ("STORE_NAME", 1),
        ("LOAD_CONST", 1),
        ("RETURN_VALUE", 1),
    ]
    if self.python_version >= (3, 11):
      expected = [("RESUME", 0)] + expected
    if self.python_version >= (3, 12):
      expected = expected[:-2] + [("RETURN_CONST", 1)]
    self.assertEqual(expected, op_and_line)

  def test_singlelinenowithspace(self):
    code = self._compile(
        "\n"
        "\n"
        "a = 1\n"  # line 3
    )
    self.assertIn("a", code.co_names)
    op_and_line = [(op.name, op.line) for op in opcodes.dis(code)]
    expected = [
        ("LOAD_CONST", 3),
        ("STORE_NAME", 3),
        ("LOAD_CONST", 3),
        ("RETURN_VALUE", 3),
    ]
    if self.python_version >= (3, 11):
      expected = [("RESUME", 0)] + expected
    if self.python_version >= (3, 12):
      expected = expected[:-2] + [("RETURN_CONST", 3)]
    self.assertEqual(expected, op_and_line)


if __name__ == "__main__":
  unittest.main()
