"""Tests for pytd_tool (pytd/main.py)."""

import sys
import textwrap

from pytype.platform_utils import path_utils
from pytype.pytd import main as pytd_tool
from pytype.tests import test_utils

import unittest


class TestPytdTool(unittest.TestCase):
  """Test pytd/main.py."""

  def setUp(self):
    super().setUp()
    # Save the value of sys.argv (which will be restored in tearDown), so that
    # tests can overwrite it.
    self._sys_argv = sys.argv

  def tearDown(self):
    super().tearDown()
    sys.argv = self._sys_argv

  def test_parse_opts(self):
    argument_parser = pytd_tool.make_parser()
    opts = argument_parser.parse_args([
        "--optimize",
        "--lossy",
        "--max-union=42",
        "--use-abcs",
        "--remove-mutable",
        "--python_version=3.9",
        "in.pytd",
        "out.pytd",
    ])
    self.assertTrue(opts.optimize)
    self.assertTrue(opts.lossy)
    self.assertEqual(opts.max_union, 42)
    self.assertTrue(opts.use_abcs)
    self.assertTrue(opts.remove_mutable)
    self.assertEqual(opts.python_version, "3.9")
    self.assertEqual(opts.input, "in.pytd")
    self.assertEqual(opts.output, "out.pytd")

  def test_version_error(self):
    sys.argv = ["main.py", "--python_version=4.0"]
    with self.assertRaises(SystemExit):
      pytd_tool.main()

  def test_missing_input(self):
    sys.argv = ["main.py"]
    with self.assertRaises(SystemExit):
      pytd_tool.main()

  def test_parse_error(self):
    with test_utils.Tempdir() as d:
      inpath = d.create_file("in.pytd", "def f(x): str")  # malformed pytd
      sys.argv = ["main.py", inpath]
      with self.assertRaises(SystemExit):
        pytd_tool.main()

  def test_no_output(self):
    with test_utils.Tempdir() as d:
      inpath = d.create_file("in.pytd", "def f(x) -> str: ...")
      # Not specifying an output is fine; the tool simply checks that the input
      # file is parseable.
      sys.argv = ["main.py", inpath]
      pytd_tool.main()

  def test_output(self):
    with test_utils.Tempdir() as d:
      src = textwrap.dedent("""
        from typing import overload

        @overload
        def f(x: int) -> str: ...
        @overload
        def f(x: str) -> str: ...
      """).strip()
      inpath = d.create_file("in.pytd", src)
      outpath = path_utils.join(d.path, "out.pytd")
      sys.argv = ["main.py", inpath, outpath]
      pytd_tool.main()
      with open(outpath) as f:
        self.assertMultiLineEqual(f.read(), src)


if __name__ == "__main__":
  unittest.main()
