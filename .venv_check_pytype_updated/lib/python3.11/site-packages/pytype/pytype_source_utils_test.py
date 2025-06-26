"""Tests for pytype_source_utils.py."""

import os

from pytype import pytype_source_utils
from pytype.platform_utils import path_utils

import unittest


class PytypeSourceUtilsTest(unittest.TestCase):
  """Test pytype source utilities."""

  def setUp(self):
    super().setUp()
    self.root = path_utils.dirname(__file__)

  def test__pytype_source_dir(self):
    self.assertEqual(self.root, pytype_source_utils._pytype_source_dir())

  def test_get_full_path(self):
    self.assertEqual(
        path_utils.join(self.root, f"foo{os.path.sep}bar"),
        pytype_source_utils.get_full_path(f"foo{os.path.sep}bar"),
    )
    self.assertEqual(
        f"{os.path.sep}foo{os.path.sep}bar",
        pytype_source_utils.get_full_path(f"{os.path.sep}foo{os.path.sep}bar"),
    )

  def test_list_pytype_files(self):
    l = list(pytype_source_utils.list_pytype_files(f"stubs{os.path.sep}stdlib"))
    self.assertIn("encodings.pytd", l)
    self.assertIn(f"collections{os.path.sep}__init__.pytd", l)


if __name__ == "__main__":
  unittest.main()
