"""Tests for config.py."""

import textwrap

from pytype.tests import test_utils
from pytype.tools import config

import unittest


class TestFindConfigFile(unittest.TestCase):
  """Tests for config.find_config_file."""

  def test_find(self):
    with test_utils.Tempdir() as d:
      f = d.create_file('setup.cfg')
      self.assertEqual(config.find_config_file(d.path), f)

  def test_find_from_file(self):
    with test_utils.Tempdir() as d:
      f1 = d.create_file('setup.cfg')
      f2 = d.create_file('some.py')
      self.assertEqual(config.find_config_file(f2), f1)

  def test_in_parent(self):
    with test_utils.Tempdir() as d:
      f = d.create_file('setup.cfg')
      path = d.create_directory('foo')
      self.assertEqual(config.find_config_file(path), f)

  def test_multiple_configs(self):
    with test_utils.Tempdir() as d:
      f1 = d.create_file('setup.cfg')
      path = d.create_directory('foo')
      f2 = d.create_file('foo/setup.cfg')
      self.assertEqual(config.find_config_file(d.path), f1)
      self.assertEqual(config.find_config_file(path), f2)

  def test_no_config(self):
    with test_utils.Tempdir() as d:
      self.assertIsNone(config.find_config_file(d.path))

  def test_pyproject_toml(self):
    with test_utils.Tempdir() as d:
      f = d.create_file('pyproject.toml')
      self.assertEqual(config.find_config_file(d.path), f)

  def test_multiple_formats_same_directory(self):
    with test_utils.Tempdir() as d:
      _ = d.create_file('setup.cfg')
      f = d.create_file('pyproject.toml')
      self.assertEqual(config.find_config_file(d.path), f)

  def test_multiple_formats_different_directories(self):
    with test_utils.Tempdir() as d1:
      f1 = d1.create_file('pyproject.toml')
      d2_path = d1.create_directory('foo')
      f2 = d1.create_file('foo/setup.cfg')
      self.assertEqual(config.find_config_file(d1.path), f1)
      self.assertEqual(config.find_config_file(d2_path), f2)


class TestIniConfigSection(unittest.TestCase):

  def test_items(self):
    with test_utils.Tempdir() as d:
      f = d.create_file(
          'setup.cfg',
          textwrap.dedent("""
        [test]
        k1 = v1
        k2 = v2
      """),
      )
      section = config.IniConfigSection.create_from_file(f, 'test')
    self.assertSequenceEqual(section.items(), [('k1', 'v1'), ('k2', 'v2')])

  def test_empty(self):
    with test_utils.Tempdir() as d:
      f = d.create_file(
          'setup.cfg',
          textwrap.dedent("""
        [test]
        k =
      """),
      )
      section = config.IniConfigSection.create_from_file(f, 'test')
      self.assertSequenceEqual(section.items(), [('k', '')])

  def test_no_file(self):
    self.assertIsNone(
        config.IniConfigSection.create_from_file('/does/not/exist.cfg', 'test')
    )

  def test_malformed_file(self):
    with test_utils.Tempdir() as d:
      f = d.create_file('setup.cfg', 'rainbow = unicorns')
      self.assertIsNone(config.IniConfigSection.create_from_file(f, 'test'))

  def test_missing_section(self):
    with test_utils.Tempdir() as d:
      f = d.create_file('setup.cfg')
      self.assertIsNone(config.IniConfigSection.create_from_file(f, 'test'))


class TestTomlConfigSection(unittest.TestCase):

  def test_items(self):
    with test_utils.Tempdir() as d:
      f = d.create_file(
          'pyproject.toml',
          textwrap.dedent("""
        [tool.test]
        k1 = 'v1'
        k2 = true
        k3 = [3, 4]
      """),
      )
      section = config.TomlConfigSection.create_from_file(f, 'test')
    self.assertSequenceEqual(
        list(section.items()), [('k1', 'v1'), ('k2', 'True'), ('k3', '3 4')]
    )

  def test_malformed_file(self):
    with test_utils.Tempdir() as d:
      f = d.create_file('pyproject.toml', 'rainbow = unicorns')
      self.assertIsNone(config.TomlConfigSection.create_from_file(f, 'test'))

  def test_missing_section(self):
    with test_utils.Tempdir() as d:
      f = d.create_file('pyproject.toml')
      self.assertIsNone(config.TomlConfigSection.create_from_file(f, 'test'))


if __name__ == '__main__':
  unittest.main()
