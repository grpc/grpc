"""Tests for module_utils.py."""

from pytype import file_utils
from pytype import module_utils
from pytype.platform_utils import path_utils

import unittest


class ModuleUtilsTest(unittest.TestCase):
  """Test module utilities."""

  def test_get_absolute_name(self):
    test_cases = [
        ("x.y", "a.b", "x.y.a.b"),
        ("", "a.b", "a.b"),
        ("x.y", ".a.b", "x.y.a.b"),
        ("x.y", "..a.b", "x.a.b"),
        ("x.y", "...a.b", None),
    ]
    for prefix, name, expected in test_cases:
      self.assertEqual(module_utils.get_absolute_name(prefix, name), expected)

  def test_get_relative_name(self):
    test_cases = [
        ("x.y", "x.y.a.b", "a.b"),
        ("x.y", "x.a.b", "..a.b"),
        ("x.y.z", "x.a.b", "...a.b"),
        ("x.y", "a.b", "a.b"),
        ("x.y", "y.a.b", "y.a.b"),
        ("x.y", "..x.y.a.b", "..x.y.a.b"),
        ("", "a.b", "a.b"),
        ("x.y", "", ""),
    ]
    for prefix, name, expected in test_cases:
      self.assertEqual(module_utils.get_relative_name(prefix, name), expected)

  def test_path_to_module_name(self):
    self.assertIsNone(
        module_utils.path_to_module_name(
            file_utils.replace_separator("../foo.py")
        )
    )
    self.assertIsNone(
        module_utils.path_to_module_name(
            file_utils.replace_separator("x/y/foo.txt")
        )
    )
    self.assertEqual(
        "x.y.z",
        module_utils.path_to_module_name(
            file_utils.replace_separator("x/y/z.pyi")
        ),
    )
    self.assertEqual(
        "x.y.z",
        module_utils.path_to_module_name(
            file_utils.replace_separator("x/y/z.pytd")
        ),
    )
    self.assertEqual(
        "x.y.z",
        module_utils.path_to_module_name(
            file_utils.replace_separator("x/y/z/__init__.pyi")
        ),
    )


# Because TestInferModule expands a lot of paths:
expand = file_utils.expand_path


class TestInferModule(unittest.TestCase):
  """Test module_utils.infer_module."""

  def assert_module_equal(self, module, path, target, name, kind="Local"):
    self.assertEqual(
        module.path.rstrip(path_utils.sep), path.rstrip(path_utils.sep)
    )
    self.assertEqual(module.target, target)
    self.assertEqual(module.name, name)
    self.assertEqual(module.kind, kind)

  def test_simple_name(self):
    mod = module_utils.infer_module(
        expand(file_utils.replace_separator("foo/bar.py")), [expand("foo")]
    )
    self.assert_module_equal(mod, expand("foo"), "bar.py", "bar")

  def test_name_in_package(self):
    mod = module_utils.infer_module(
        expand(file_utils.replace_separator("foo/bar/baz.py")), [expand("foo")]
    )
    self.assert_module_equal(
        mod,
        expand("foo"),
        file_utils.replace_separator("bar/baz.py"),
        "bar.baz",
    )

  def test_multiple_paths(self):
    pythonpath = [
        expand("foo"),
        expand(file_utils.replace_separator("bar/baz")),
        expand("bar"),
    ]
    mod = module_utils.infer_module(
        expand(file_utils.replace_separator("bar/baz/qux.py")), pythonpath
    )
    self.assert_module_equal(
        mod, expand(file_utils.replace_separator("bar/baz")), "qux.py", "qux"
    )
    mod = module_utils.infer_module(
        expand(file_utils.replace_separator("bar/qux.py")), pythonpath
    )
    self.assert_module_equal(mod, expand("bar"), "qux.py", "qux")

  def test_not_found(self):
    mod = module_utils.infer_module(
        expand(file_utils.replace_separator("bar/baz.py")), ["foo"]
    )
    expected_target = expand(file_utils.replace_separator("bar/baz.py"))
    expected_name, _ = path_utils.splitext(
        expected_target.replace(path_utils.sep, ".")
    )
    self.assert_module_equal(mod, "", expected_target, expected_name)


if __name__ == "__main__":
  unittest.main()
