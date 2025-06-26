"""Tests for io.py."""

import contextlib
import io as builtins_io
import sys
import textwrap
import traceback

from pytype import config
from pytype import file_utils
from pytype import io
from pytype.platform_utils import path_utils
from pytype.platform_utils import tempfile as compatible_tempfile
from pytype.pytd import pytd
from pytype.tests import test_utils

import unittest


class IOTest(unittest.TestCase):
  """Test IO functions."""

  def test_read_source_file_utf8(self):
    with self._tmpfile("abc□def\n") as f:
      self.assertEqual(io.read_source_file(f.name), "abc□def\n")

  @contextlib.contextmanager
  def _tmpfile(self, contents):
    tempfile_options = {"mode": "w", "suffix": ".txt", "encoding": "utf-8"}
    with compatible_tempfile.NamedTemporaryFile(**tempfile_options) as f:
      f.write(contents)
      f.flush()
      yield f

  def test_wrap_pytype_exceptions(self):
    with self.assertRaises(ValueError):
      with io.wrap_pytype_exceptions(ValueError, "foo.py"):
        io.read_source_file("missing_file")

  def test_wrap_pytype_exception_traceback(self):
    class CustomError(Exception):
      pass

    def called_function():
      raise OSError("error!")

    def calling_function():
      called_function()

    err = None
    trace = None
    try:
      with io.wrap_pytype_exceptions(CustomError, "foo.py"):
        calling_function()
    except CustomError as e:
      err = e
      _, _, tb = sys.exc_info()
      trace = traceback.format_tb(tb)

    self.assertIn("OSError: error!", err.args[0])
    self.assertTrue(any("in calling_function" in x for x in trace))

  def test_check_py(self):
    errorlog = io.check_py("undefined_var").context.errorlog
    (error,) = errorlog.unique_sorted_errors()
    self.assertEqual(error.name, "name-error")

  def test_check_py_with_options(self):
    options = config.Options.create(disable="name-error")
    errorlog = io.check_py("undefined_var", options).context.errorlog
    self.assertFalse(errorlog.unique_sorted_errors())

  def test_generate_pyi(self):
    ret, pyi_string = io.generate_pyi("x = 42")
    self.assertFalse(ret.context.errorlog.unique_sorted_errors())
    self.assertEqual(pyi_string, "x: int\n")
    self.assertIsInstance(ret.ast, pytd.TypeDeclUnit)

  def test_generate_pyi_with_options(self):
    with self._tmpfile("x: int") as pyi:
      pyi_name, _ = path_utils.splitext(path_utils.basename(pyi.name))
      with self._tmpfile(f"{pyi_name} {pyi.name}") as imports_map:
        src = "import {mod}; y = {mod}.x".format(mod=pyi_name)
        options = config.Options.create(imports_map=imports_map.name)
        _, pyi_string = io.generate_pyi(src, options)
    self.assertEqual(pyi_string, f"import {pyi_name}\n\ny: int\n")

  def test_generate_pyi__overload_order(self):
    _, pyi_string = io.generate_pyi(textwrap.dedent("""
      from typing import Any, overload
      @overload
      def f(x: None) -> None: ...
      @overload
      def f(x: Any) -> int: ...
      def f(x):
        return __any_object__
    """.lstrip("\n")))
    self.assertMultiLineEqual(
        pyi_string,
        textwrap.dedent("""
      from typing import overload

      @overload
      def f(x: None) -> None: ...
      @overload
      def f(x) -> int: ...
    """.lstrip("\n")),
    )

  def test_check_or_generate_pyi__check(self):
    with self._tmpfile("") as f:
      options = config.Options.create(f.name, check=True)
      ret = io.check_or_generate_pyi(options)
    self.assertIsNone(ret.pyi)
    self.assertIsNone(ret.ast)

  def test_check_or_generate_pyi__generate(self):
    with self._tmpfile("") as f:
      options = config.Options.create(f.name, check=False)
      ret = io.check_or_generate_pyi(options)
    self.assertIsNotNone(ret.pyi)
    self.assertIsNotNone(ret.ast)

  def test_check_or_generate_pyi__open_function(self):
    def mock_open(filename, *args, **kwargs):
      if filename == "my_amazing_file.py":
        return builtins_io.StringIO("x = 0.0")
      else:
        return open(filename, *args, **kwargs)  # pylint: disable=consider-using-with

    options = config.Options.create(
        "my_amazing_file.py", check=False, open_function=mock_open
    )
    ret = io.check_or_generate_pyi(options)
    self.assertEqual(ret.pyi, "x: float\n")

  def test_write_pickle(self):
    ast = pytd.TypeDeclUnit(None, (), (), (), (), ())
    options = config.Options.create(
        output="/dev/null" if sys.platform != "win32" else "NUL"
    )
    io.write_pickle(ast, options)  # just make sure we don't crash

  def test_unused_imports_info_files(self):
    with test_utils.Tempdir() as d, file_utils.cd(d.path):
      d.create_file("common/foo.pyi", "from common import bar\nx: bar.Bar")
      d.create_file("common/bar.pyi", "class Bar: pass")
      d.create_file("common/baz.pyi", "class Baz: pass")
      d.create_file("aaa/other.pyi", "class Other: pass")
      imports_info = d.create_file(
          "imports_info",
          file_utils.replace_separator(textwrap.dedent("""
              common/foo common/foo.pyi
              common/bar common/bar.pyi
              common/baz common/baz.pyi
              aaa/other aaa/other.pyi
          """)),
      )
      module = d.create_file("m.py", "from common import foo; print(foo.x)")
      unused_imports_info_files = path_utils.join(d.path, "unused_imports_info")
      options = config.Options.create(
          module,
          imports_map=imports_info,
          unused_imports_info_files=unused_imports_info_files,
      )
      ret = io.process_one_file(options)
      self.assertEqual(0, ret)
      self.assertTrue(
          path_utils.exists(unused_imports_info_files),
          f"{unused_imports_info_files!r} does not exist",
      )
      with options.open_function(unused_imports_info_files) as f:
        content = f.read()
      self.assertEqual(
          content,
          file_utils.replace_separator("aaa/other.pyi\ncommon/baz.pyi\n"),
      )


if __name__ == "__main__":
  unittest.main()
