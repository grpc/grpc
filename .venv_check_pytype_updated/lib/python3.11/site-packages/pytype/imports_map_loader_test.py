import os
import textwrap

from pytype import file_utils
from pytype import imports_map_loader
from pytype.platform_utils import path_utils
from pytype.platform_utils import tempfile as compatible_tempfile
from pytype.tests import test_utils

import unittest


class FakeOptions:
  """Fake options."""

  def __init__(self):
    self.open_function = open


def _abs_path(path: str) -> str:
  if path == os.devnull:
    return path
  return path_utils.abspath(path)


class ImportMapLoaderTest(unittest.TestCase):
  """Tests for imports_map_loader.py."""

  @classmethod
  def setUpClass(cls):
    super().setUpClass()
    cls.builder = imports_map_loader.ImportsMapBuilder(FakeOptions())

  def test_read_imports_info(self):
    """Test reading an imports_info file into ImportsInfo."""
    with compatible_tempfile.NamedTemporaryFile() as fi:
      fi.write(textwrap.dedent(file_utils.replace_separator("""
        a/b/__init__.py prefix/1/a/b/__init__.py~
        a/b/b.py prefix/1/a/b/b.py~suffix
        a/b/c.pyi prefix/1/a/b/c.pyi~
        a/b/d.py prefix/1/a/b/d.py~
        a/b/e.py 2/a/b/e1.py~
        a/b/e 2/a/b/e2.py~
        a/b/e 2/a/b/foo/#2.py~
      """)).encode("utf-8"))
      fi.seek(0)  # ready for reading
      expected_items = {
          "a/__init__": os.devnull,
          "a/b/__init__": "prefix/1/a/b/__init__.py~",
          "a/b/b": "prefix/1/a/b/b.py~suffix",
          "a/b/c": "prefix/1/a/b/c.pyi~",
          "a/b/d": "prefix/1/a/b/d.py~",
          "a/b/e": "2/a/b/foo/#2.py~",
      }
      expected_unused = [
          "2/a/b/e1.py~",
          "2/a/b/e2.py~",
      ]
      expected_items = {
          file_utils.replace_separator(k): _abs_path(v)
          for k, v in expected_items.items()
      }
      expected_unused = [
          file_utils.replace_separator(v) for v in expected_unused
      ]
      actual = self.builder.build_from_file(fi.name)
      self.assertEqual(actual.items, expected_items)
      self.assertEqual(actual.unused, expected_unused)

  def test_build_imports_info(self):
    """Test building an ImportsInfo from an imports_info tuple."""
    items = [
        ("a/b/__init__.py", "prefix/1/a/b/__init__.py~"),
        ("a/b/b.py", "prefix/1/a/b/b.py~suffix"),
        ("a/b/c.pyi", "prefix/1/a/b/c.pyi~"),
        ("a/b/d.py", "prefix/1/a/b/d.py~"),
        ("a/b/e.py", "2/a/b/e1.py~"),
        ("a/b/e", "2/a/b/e2.py~"),
        ("a/b/e", "2/a/b/foo/#2.py~"),
    ]
    items = [
        (file_utils.replace_separator(k), file_utils.replace_separator(v))
        for k, v in items
    ]
    expected_items = {
        "a/__init__": os.devnull,
        "a/b/__init__": "prefix/1/a/b/__init__.py~",
        "a/b/b": "prefix/1/a/b/b.py~suffix",
        "a/b/c": "prefix/1/a/b/c.pyi~",
        "a/b/d": "prefix/1/a/b/d.py~",
        "a/b/e": "2/a/b/foo/#2.py~",
    }
    expected_unused = [
        "2/a/b/e1.py~",
        "2/a/b/e2.py~",
    ]
    expected_items = {
        file_utils.replace_separator(k): _abs_path(v)
        for k, v in expected_items.items()
    }
    expected_unused = [file_utils.replace_separator(v) for v in expected_unused]
    actual = self.builder.build_from_items(items)
    self.assertEqual(actual.items, expected_items)
    self.assertEqual(actual.unused, expected_unused)

  def test_do_not_filter(self):
    with test_utils.Tempdir() as d:
      d.create_file(file_utils.replace_separator("a/b/c.pyi"))
      imports_info = (
          f"{file_utils.replace_separator('a/b/c.pyi')} "
          + f"{d[file_utils.replace_separator('a/b/c.pyi')]}\n"
      )
      d.create_file("imports_info", imports_info)
      imports_map = self.builder.build_from_file(d["imports_info"])
      self.assertEqual(
          imports_map[file_utils.replace_separator("a/b/c")],
          d[file_utils.replace_separator("a/b/c.pyi")],
      )

  def test_invalid_map_entry(self):
    with test_utils.Tempdir() as d:
      imports_info = (
          f"{file_utils.replace_separator('a/b/c.pyi')}"
          f"{d[file_utils.replace_separator('a/b/c.pyi')]}\n"
      )
      d.create_file("imports_info", imports_info)
      with self.assertRaises(ValueError):
        self.builder.build_from_file(d["imports_info"])


if __name__ == "__main__":
  unittest.main()
