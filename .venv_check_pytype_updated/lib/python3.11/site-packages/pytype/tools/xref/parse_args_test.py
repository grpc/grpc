"""Tests for parse_args.py."""

from pytype.tests import test_utils
from pytype.tools.xref import parse_args
import unittest


class TestParseArgs(unittest.TestCase):
  """Test parse_args.parse_args."""

  def test_parse_filename(self):
    _, _, pytype_opts = parse_args.parse_args(["a.py"])
    self.assertEqual(pytype_opts.input, "a.py")

  def test_parse_no_filename(self):
    with self.assertRaises(SystemExit):
      parse_args.parse_args([])

  def test_kythe_args(self):
    _, kythe_args, _ = parse_args.parse_args(
        ["a.py",
         "--kythe_corpus", "foo",
         "--kythe_root", "bar",
         "--kythe_path", "baz"])
    self.assertEqual(kythe_args.corpus, "foo")
    self.assertEqual(kythe_args.root, "bar")
    self.assertEqual(kythe_args.path, "baz")

  def test_imports_info(self):
    # The code reads and validates an import map within pytype's setup, so we
    # need to provide a syntactically valid one as a file.
    with test_utils.Tempdir() as d:
      pyi_file = d.create_file("baz.pyi")
      imports_info = d.create_file("foo", f"bar {pyi_file}")
      _, _, opts = parse_args.parse_args(
          ["a.py", "--imports_info", imports_info])
      self.assertEqual(opts.imports_map.items, {"bar": pyi_file})
      self.assertTrue(opts.use_pickled_files)


if __name__ == "__main__":
  unittest.main()
