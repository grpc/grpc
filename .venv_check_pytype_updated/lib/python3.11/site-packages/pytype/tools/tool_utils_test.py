"""Tests for tool_utils.py."""

import sys

from pytype.platform_utils import path_utils
from pytype.tests import test_utils
from pytype.tools import tool_utils

import unittest


class TestSetupLoggingOrDie(unittest.TestCase):
  """Tests for tool_utils.setup_logging_or_die."""

  def test_negative_verbosity(self):
    with self.assertRaises(SystemExit):
      tool_utils.setup_logging_or_die(-1)

  def test_excessive_verbosity(self):
    with self.assertRaises(SystemExit):
      tool_utils.setup_logging_or_die(3)

  def test_set_level(self):
    # Log level can't actually be set in a test, so we're just testing that the
    # code doesn't blow up.
    tool_utils.setup_logging_or_die(0)
    tool_utils.setup_logging_or_die(1)
    tool_utils.setup_logging_or_die(2)


class TestMakeDirsOrDie(unittest.TestCase):
  """Tests for tool_utils.makedirs_or_die()."""

  def test_make(self):
    with test_utils.Tempdir() as d:
      subdir = path_utils.join(d.path, 'some/path')
      tool_utils.makedirs_or_die(subdir, '')
      self.assertTrue(path_utils.isdir(subdir))

  def test_die(self):
    with self.assertRaises(SystemExit):
      if sys.platform == 'win32':
        tool_utils.makedirs_or_die('C:/invalid:path', '')
      else:
        tool_utils.makedirs_or_die('/nonexistent/path', '')


if __name__ == '__main__':
  unittest.main()
