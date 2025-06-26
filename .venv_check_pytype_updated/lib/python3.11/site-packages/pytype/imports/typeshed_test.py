"""Tests for typeshed.py."""

import os

from pytype import file_utils
from pytype.imports import builtin_stubs
from pytype.imports import typeshed
from pytype.platform_utils import path_utils
from pytype.pytd.parse import parser_test_base
from pytype.tests import test_base
from pytype.tests import test_utils


class TypeshedTestFs(typeshed.ExternalTypeshedFs):
  """Filestore with configurable root dir."""

  def __init__(self, root):
    self._test_root = root
    super().__init__()

  def get_root(self):
    return self._test_root


class TestTypeshedLoading(parser_test_base.ParserTest):
  """Test the code for loading files from typeshed."""

  def setUp(self):
    super().setUp()
    self.ts = typeshed.Typeshed()

  def test_get_typeshed_file(self):
    filename, data = self.ts.get_module_file(
        "stdlib", "errno", self.python_version
    )
    self.assertEqual(file_utils.replace_separator("stdlib/errno.pyi"), filename)
    self.assertIn("errorcode", data)

  def test_get_typeshed_dir(self):
    filename, data = self.ts.get_module_file(
        "stdlib", "logging", self.python_version
    )
    self.assertEqual(
        file_utils.replace_separator("stdlib/logging/__init__.pyi"), filename
    )
    self.assertIn("LogRecord", data)

  def test_load_module(self):
    loader = typeshed.TypeshedLoader(self.options, ())
    filename, ast = loader.load_module("stdlib", "_random")
    self.assertEqual(
        filename, file_utils.replace_separator("stdlib/_random.pyi")
    )
    self.assertIn("_random.Random", [cls.name for cls in ast.classes])

  def test_get_typeshed_missing(self):
    if not self.ts.missing:
      return  # nothing to test
    self.assertIn(path_utils.join("stdlib", "pytypecanary"), self.ts.missing)
    _, data = self.ts.get_module_file(
        "stdlib", "pytypecanary", self.python_version
    )
    self.assertEqual(data, builtin_stubs.DEFAULT_SRC)

  def test_get_google_only_module_names(self):
    if not self.ts.missing:
      return  # nothing to test
    modules = self.ts.get_all_module_names(self.python_version)
    self.assertIn("pytypecanary", modules)

  def test_get_all_module_names(self):
    modules = self.ts.get_all_module_names((3, 10))
    self.assertIn("asyncio", modules)
    self.assertIn("collections", modules)
    self.assertIn("configparser", modules)

  def test_get_pytd_paths(self):
    # Set TYPESHED_HOME to pytype's internal typeshed copy.
    old_env = os.environ.copy()
    os.environ["TYPESHED_HOME"] = self.ts._store._root
    try:
      # Check that get_pytd_paths() works with a typeshed installation that
      # reads from TYPESHED_HOME.

      paths = {
          p.rsplit(file_utils.replace_separator("pytype/"), 1)[-1]
          for p in self.ts.get_pytd_paths()
      }
      self.assertSetEqual(
          paths,
          {
              file_utils.replace_separator("stubs/builtins"),
              file_utils.replace_separator("stubs/stdlib"),
          },
      )
    finally:
      os.environ = old_env

  def test_read_blacklist(self):
    for filename in self.ts.read_blacklist():
      self.assertTrue(
          filename.startswith("stdlib") or filename.startswith("stubs")
      )

  def test_blacklisted_modules(self):
    for module_name in self.ts.blacklisted_modules():
      self.assertNotIn("/", module_name)

  def test_carriage_return(self):
    self.ts._stdlib_versions["foo"] = ((3, 10), None)
    with test_utils.Tempdir() as d:
      d.create_file(
          file_utils.replace_separator("stdlib/foo.pyi"), b"x: int\r\n"
      )
      self.ts._store = TypeshedTestFs(d.path)
      filename, src = self.ts.get_module_file("stdlib", "foo", (3, 10))
    self.assertEqual(file_utils.replace_separator("stdlib/foo.pyi"), filename)
    self.assertEqual(src, "x: int\n")


if __name__ == "__main__":
  test_base.main()
