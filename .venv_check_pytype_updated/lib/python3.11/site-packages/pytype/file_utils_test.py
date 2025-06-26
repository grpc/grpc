"""Tests for file_utils.py."""

from pytype import file_utils
from pytype.platform_utils import path_utils
from pytype.tests import test_utils

import unittest


class FileUtilsTest(unittest.TestCase):
  """Test file and path utilities."""

  def test_replace_extension(self):
    self.assertEqual("foo.bar", file_utils.replace_extension("foo.txt", "bar"))
    self.assertEqual("foo.bar", file_utils.replace_extension("foo.txt", ".bar"))
    self.assertEqual(
        "a.b.c.bar", file_utils.replace_extension("a.b.c.txt", ".bar")
    )
    self.assertEqual(
        file_utils.replace_separator("a.b/c.bar"),
        file_utils.replace_extension(
            file_utils.replace_separator("a.b/c.d"), ".bar"
        ),
    )
    self.assertEqual("xyz.bar", file_utils.replace_extension("xyz", "bar"))

  def test_tempdir(self):
    with test_utils.Tempdir() as d:
      filename1 = d.create_file("foo.txt")
      filename2 = d.create_file("bar.txt", "\tdata2")
      filename3 = d.create_file("baz.txt", "data3")
      filename4 = d.create_file(
          file_utils.replace_separator("d1/d2/qqsv.txt"), "  data4.1\n  data4.2"
      )
      filename5 = d.create_directory("directory")
      self.assertEqual(filename1, d["foo.txt"])
      self.assertEqual(filename2, d["bar.txt"])
      self.assertEqual(filename3, d["baz.txt"])
      self.assertEqual(
          filename4, d[file_utils.replace_separator("d1/d2/qqsv.txt")]
      )
      self.assertTrue(path_utils.isdir(d.path))
      self.assertTrue(path_utils.isfile(filename1))
      self.assertTrue(path_utils.isfile(filename2))
      self.assertTrue(path_utils.isfile(filename3))
      self.assertTrue(path_utils.isfile(filename4))
      self.assertTrue(path_utils.isdir(path_utils.join(d.path, "d1")))
      self.assertTrue(path_utils.isdir(path_utils.join(d.path, "d1", "d2")))
      self.assertTrue(path_utils.isdir(filename5))
      self.assertEqual(
          filename4, path_utils.join(d.path, "d1", "d2", "qqsv.txt")
      )
      for filename, contents in [
          (filename1, ""),
          (filename2, "data2"),  # dedented
          (filename3, "data3"),
          (filename4, "data4.1\ndata4.2"),  # dedented
      ]:
        with open(filename) as fi:
          self.assertEqual(fi.read(), contents)
    self.assertFalse(path_utils.isdir(d.path))
    self.assertFalse(path_utils.isfile(filename1))
    self.assertFalse(path_utils.isfile(filename2))
    self.assertFalse(path_utils.isfile(filename3))
    self.assertFalse(path_utils.isdir(path_utils.join(d.path, "d1")))
    self.assertFalse(path_utils.isdir(path_utils.join(d.path, "d1", "d2")))
    self.assertFalse(path_utils.isdir(filename5))

  def test_cd(self):
    with test_utils.Tempdir() as d:
      d.create_directory("foo")
      d1 = path_utils.getcwd()
      with file_utils.cd(d.path):
        self.assertTrue(path_utils.isdir("foo"))
      d2 = path_utils.getcwd()
      self.assertEqual(d1, d2)

  def test_cd_noop(self):
    d = path_utils.getcwd()
    with file_utils.cd(None):
      self.assertEqual(path_utils.getcwd(), d)
    with file_utils.cd(""):
      self.assertEqual(path_utils.getcwd(), d)


class TestPathExpansion(unittest.TestCase):
  """Tests for file_utils.expand_path(s?)."""

  def test_expand_one_path(self):
    full_path = path_utils.join(path_utils.getcwd(), "foo.py")
    self.assertEqual(file_utils.expand_path("foo.py"), full_path)

  def test_expand_two_paths(self):
    full_path1 = path_utils.join(path_utils.getcwd(), "foo.py")
    full_path2 = path_utils.join(path_utils.getcwd(), "bar.py")
    self.assertEqual(
        file_utils.expand_paths(["foo.py", "bar.py"]), [full_path1, full_path2]
    )

  def test_expand_with_cwd(self):
    with test_utils.Tempdir() as d:
      f = d.create_file("foo.py")
      self.assertEqual(
          file_utils.expand_path("foo.py", d.path),
          file_utils.expand_path(f),
      )


class TestExpandSourceFiles(unittest.TestCase):
  """Tests for file_utils.expand_source_files."""

  FILES = [
      "a.py",
      file_utils.replace_separator("foo/b.py"),
      file_utils.replace_separator("foo/c.txt"),
      file_utils.replace_separator("foo/bar/d.py"),
      file_utils.replace_separator("foo/bar/baz/e.py"),
  ]

  def _test_expand(self, string):
    with test_utils.Tempdir() as d:
      fs = [d.create_file(f) for f in self.FILES]
      pyfiles = file_utils.expand_paths(f for f in fs if f.endswith(".py"))
      self.assertCountEqual(
          pyfiles, file_utils.expand_source_files(string, d.path)
      )

  def test_expand_source_files(self):
    self._test_expand(file_utils.replace_separator("a.py foo/c.txt foo"))

  def test_duplicates(self):
    self._test_expand(file_utils.replace_separator("a.py foo/b.py foo foo/bar"))

  def test_cwd(self):
    with test_utils.Tempdir() as d:
      fs = [d.create_file(f) for f in self.FILES]
      pyfiles = file_utils.expand_paths(f for f in fs if f.endswith(".py"))
      # cd to d.path and run with just "." as an argument
      with file_utils.cd(d.path):
        self.assertCountEqual(pyfiles, file_utils.expand_source_files("."))

  def test_empty(self):
    self.assertEqual(file_utils.expand_source_files(""), set())

  def test_magic(self):
    filenames = ["a.py", file_utils.replace_separator("b/c.py")]
    with test_utils.Tempdir() as d:
      for f in filenames:
        d.create_file(f)
      with file_utils.cd(d.path):
        self.assertEqual(
            file_utils.expand_source_files(
                file_utils.replace_separator("**/*.py")
            ),
            {path_utils.realpath(f) for f in filenames},
        )

  def test_magic_with_cwd(self):
    filenames = ["a.py", file_utils.replace_separator("b/c.py")]
    with test_utils.Tempdir() as d:
      for f in filenames:
        d.create_file(f)
      self.assertEqual(
          file_utils.expand_source_files(
              file_utils.replace_separator("**/*.py"), cwd=d.path
          ),
          set(
              file_utils.expand_paths(
                  path_utils.join(d.path, f) for f in filenames
              )
          ),
      )

  def test_multiple_magic(self):
    filenames = ["a.py", file_utils.replace_separator("b/c.py")]
    with test_utils.Tempdir() as d:
      for f in filenames:
        d.create_file(f)
      self.assertEqual(
          file_utils.expand_source_files(
              file_utils.replace_separator("*.py b/*.py"), cwd=d.path
          ),
          set(
              file_utils.expand_paths(
                  path_utils.join(d.path, f) for f in filenames
              )
          ),
      )


class TestExpandHiddenFiles(unittest.TestCase):

  def test_ignore_file(self):
    with test_utils.Tempdir() as d:
      d.create_file(".ignore.py")
      self.assertEqual(file_utils.expand_source_files(".", cwd=d.path), set())

  def test_find_file(self):
    with test_utils.Tempdir() as d:
      d.create_file(".find.py")
      self.assertEqual(
          file_utils.expand_source_files(".*", cwd=d.path),
          {file_utils.expand_path(path_utils.join(d.path, ".find.py"))},
      )

  def test_ignore_dir(self):
    with test_utils.Tempdir() as d:
      d.create_file(file_utils.replace_separator("d1/.d2/ignore.py"))
      self.assertEqual(
          file_utils.expand_source_files(
              file_utils.replace_separator("d1/**/*"), cwd=d.path
          ),
          set(),
      )

  def test_find_dir(self):
    with test_utils.Tempdir() as d:
      d.create_file(file_utils.replace_separator(".d/find.py"))
      self.assertEqual(
          file_utils.expand_source_files(
              file_utils.replace_separator(".d/**/*"), cwd=d.path
          ),
          {file_utils.expand_path(path_utils.join(d.path, ".d", "find.py"))},
      )


class TestExpandPythonpath(unittest.TestCase):

  def test_expand(self):
    self.assertEqual(
        file_utils.expand_pythonpath(file_utils.replace_separator("a/b:c/d")),
        [
            path_utils.join(path_utils.getcwd(), "a", "b"),
            path_utils.join(path_utils.getcwd(), "c", "d"),
        ],
    )

  def test_expand_empty(self):
    self.assertEqual(file_utils.expand_pythonpath(""), [])

  def test_expand_current_directory(self):
    self.assertEqual(
        file_utils.expand_pythonpath(file_utils.replace_separator(":a")),
        [path_utils.getcwd(), path_utils.join(path_utils.getcwd(), "a")],
    )

  def test_expand_with_cwd(self):
    with test_utils.Tempdir() as d:
      self.assertEqual(
          file_utils.expand_pythonpath(
              file_utils.replace_separator("a/b:c/d"), cwd=d.path
          ),
          file_utils.expand_paths([
              path_utils.join(d.path, "a", "b"),
              path_utils.join(d.path, "c", "d"),
          ]),
      )

  def test_strip_whitespace(self):
    self.assertEqual(
        file_utils.expand_pythonpath(file_utils.replace_separator("""
      a/b:
      c/d
    """)),
        [
            path_utils.join(path_utils.getcwd(), "a", "b"),
            path_utils.join(path_utils.getcwd(), "c", "d"),
        ],
    )


class TestExpandGlobpaths(unittest.TestCase):

  def test_expand_empty(self):
    self.assertEqual(file_utils.expand_globpaths([]), [])

  def test_expand(self):
    filenames = ["a.py", file_utils.replace_separator("b/c.py")]
    with test_utils.Tempdir() as d:
      for f in filenames:
        d.create_file(f)
      with file_utils.cd(d.path):
        self.assertEqual(
            file_utils.expand_globpaths(
                [file_utils.replace_separator("**/*.py")]
            ),
            [path_utils.realpath(f) for f in filenames],
        )

  def test_expand_with_cwd(self):
    filenames = ["a.py", file_utils.replace_separator("b/c.py")]
    with test_utils.Tempdir() as d:
      for f in filenames:
        d.create_file(f)
      self.assertEqual(
          file_utils.expand_globpaths(
              [file_utils.replace_separator("**/*.py")], cwd=d.path
          ),
          file_utils.expand_paths(
              path_utils.join(d.path, f) for f in filenames
          ),
      )


if __name__ == "__main__":
  unittest.main()
