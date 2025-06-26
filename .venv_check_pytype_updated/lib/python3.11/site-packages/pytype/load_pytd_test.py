"""Tests for load_pytd.py."""

import contextlib
import dataclasses
import io
import os
import sys
import textwrap

from pytype import config
from pytype import file_utils
from pytype import imports_map
from pytype import load_pytd
from pytype import module_utils
from pytype.imports import pickle_utils
from pytype.platform_utils import path_utils
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.tests import test_base
from pytype.tests import test_utils

import unittest


class ModuleTest(test_base.UnitTest):
  """Tests for load_pytd.Module."""

  def test_is_package(self):
    for filename, is_package in [
        ("foo/bar.pyi", False),
        ("foo/__init__.pyi", True),
        ("foo/__init__.pyi-1", True),
        ("foo/__init__.pickled", True),
        (os.devnull, True),
    ]:
      with self.subTest(filename=filename):
        mod = load_pytd.Module(module_name=None, filename=filename, ast=None)
        self.assertEqual(mod.is_package(), is_package)


class _LoaderTest(test_base.UnitTest):

  @contextlib.contextmanager
  def _setup_loader(self, **kwargs):
    with test_utils.Tempdir() as d:
      for name, contents in kwargs.items():
        d.create_file(f"{name}.pyi", contents)
      yield load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )

  def _import(self, **kwargs):
    with self._setup_loader(**kwargs) as loader:
      return loader.import_name(kwargs.popitem()[0])


class ImportPathsTest(_LoaderTest):
  """Tests for load_pytd.py."""

  def test_filepath_to_module(self):
    # (filename, pythonpath, expected)
    test_cases = [
        ("foo/bar/baz.py", [""], "foo.bar.baz"),
        ("foo/bar/baz.py", ["foo"], "bar.baz"),
        ("foo/bar/baz.py", ["fo"], "foo.bar.baz"),
        ("foo/bar/baz.py", ["foo/"], "bar.baz"),
        ("foo/bar/baz.py", ["foo", "bar"], "bar.baz"),
        ("foo/bar/baz.py", ["foo/bar", "foo"], "baz"),
        ("foo/bar/baz.py", ["foo", "foo/bar"], "bar.baz"),
        ("./foo/bar.py", [""], "foo.bar"),
        ("./foo.py", [""], "foo"),
        ("../foo.py", [""], None),
        ("../foo.py", ["."], None),
        ("foo/bar/../baz.py", [""], "foo.baz"),
        ("../foo.py", [".."], "foo"),
        ("../../foo.py", ["../.."], "foo"),
        ("../../foo.py", [".."], None),
    ]
    replaced_test_cased = []
    for a, b, c in test_cases:
      replaced_test_cased.append((
          file_utils.replace_separator(a),
          list(map(file_utils.replace_separator, b)),
          c,
      ))
    test_cases = replaced_test_cased

    for filename, pythonpath, expected in test_cases:
      module = module_utils.get_module_name(filename, pythonpath)
      self.assertEqual(module, expected)

  def test_builtin_sys(self):
    with self._setup_loader() as loader:
      ast = loader.import_name("sys")
      self.assertTrue(ast.Lookup("sys.exit"))

  def test_basic(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          file_utils.replace_separator("path/to/some/module.pyi"),
          "def foo(x:int) -> str: ...",
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      ast = loader.import_name("path.to.some.module")
      self.assertTrue(ast.Lookup("path.to.some.module.foo"))

  def test_path(self):
    with test_utils.Tempdir() as d1:
      with test_utils.Tempdir() as d2:
        d1.create_file(
            file_utils.replace_separator("dir1/module1.pyi"),
            "def foo1() -> str: ...",
        )
        d2.create_file(
            file_utils.replace_separator("dir2/module2.pyi"),
            "def foo2() -> str: ...",
        )
        loader = load_pytd.Loader(
            config.Options.create(
                module_name="base",
                python_version=self.python_version,
                pythonpath=f"{d1.path}{os.pathsep}{d2.path}",
            )
        )
        module1 = loader.import_name("dir1.module1")
        module2 = loader.import_name("dir2.module2")
        self.assertTrue(module1.Lookup("dir1.module1.foo1"))
        self.assertTrue(module2.Lookup("dir2.module2.foo2"))

  def test_init(self):
    with test_utils.Tempdir() as d1:
      d1.create_file(
          file_utils.replace_separator("baz/__init__.pyi"),
          "x = ... # type: int",
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d1.path,
          )
      )
      self.assertTrue(loader.import_name("baz").Lookup("baz.x"))

  def test_builtins(self):
    with test_utils.Tempdir() as d:
      d.create_file("foo.pyi", "x = ... # type: int")
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      mod = loader.import_name("foo")
      self.assertEqual("builtins.int", mod.Lookup("foo.x").type.cls.name)
      self.assertEqual("builtins.int", mod.Lookup("foo.x").type.name)

  def test_no_init(self):
    with test_utils.Tempdir() as d:
      d.create_directory("baz")
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      self.assertTrue(loader.import_name("baz"))

  def test_no_init_imports_map(self):
    with test_utils.Tempdir() as d:
      d.create_directory("baz")
      with file_utils.cd(d.path):
        loader = load_pytd.Loader(
            config.Options.create(
                module_name="base",
                python_version=self.python_version,
                pythonpath="",
            )
        )
        loader.options.tweak(imports_map=imports_map.ImportsMap())
        self.assertFalse(loader.import_name("baz"))

  def test_stdlib(self):
    with self._setup_loader() as loader:
      ast = loader.import_name("io")
      self.assertTrue(ast.Lookup("io.StringIO"))

  def test_deep_dependency(self):
    with test_utils.Tempdir() as d:
      d.create_file("module1.pyi", "def get_bar() -> module2.Bar: ...")
      d.create_file("module2.pyi", "class Bar:\n  pass")
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      module1 = loader.import_name("module1")
      (f,) = module1.Lookup("module1.get_bar").signatures
      self.assertEqual("module2.Bar", f.return_type.cls.name)

  def test_circular_dependency(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          "foo.pyi",
          """
        def get_bar() -> bar.Bar: ...
        class Foo:
          pass
      """,
      )
      d.create_file(
          "bar.pyi",
          """
        def get_foo() -> foo.Foo: ...
        class Bar:
          pass
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      foo = loader.import_name("foo")
      bar = loader.import_name("bar")
      (f1,) = foo.Lookup("foo.get_bar").signatures
      (f2,) = bar.Lookup("bar.get_foo").signatures
      self.assertEqual("bar.Bar", f1.return_type.cls.name)
      self.assertEqual("foo.Foo", f2.return_type.cls.name)

  def test_circular_dependency_complicated(self):
    # The dependency graph looks like:
    # target ----------
    # |               |
    # v               v
    # dep1 -> dep2 -> dep3
    # ^               |
    # |               |
    # -----------------
    with self._setup_loader(
        target="""
      from dep1 import PathLike
      from dep3 import AnyPath
      def abspath(path: PathLike[str]) -> str: ...
    """,
        dep1="""
      from dep2 import Popen
      from typing import Generic, TypeVar
      _T = TypeVar('_T')
      class PathLike(Generic[_T]): ...
    """,
        dep2="""
      from dep3 import AnyPath
      class Popen: ...
    """,
        dep3="""
      from dep1 import PathLike
      AnyPath = PathLike[str]
    """,
    ) as loader:
      loader.finish_and_verify_ast(
          loader.load_file(
              "target",
              path_utils.join(loader.options.pythonpath[0], "target.pyi"),
          )
      )

  def test_circular_dependency_with_type_param(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          "bar.pyi",
          """
          from typing import Callable, ParamSpec

          from foo import Foo

          _P = ParamSpec("_P")

          class Bar:
            foo: Foo | None
          def bar(obj: Callable[_P, None], /, *args: _P.args, **kwargs: _P.kwargs) -> Bar: ...
          """,
      )
      d.create_file(
          "foo.pyi",
          """
          from bar import bar as _bar

          class Foo: ...
          bar = _bar
          """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      bar = loader.import_name("bar")
      foo = loader.import_name("foo")
      self.assertTrue(bar.Lookup("bar.bar"))
      self.assertTrue(foo.Lookup("foo.bar"))

  def test_cache(self):
    with test_utils.Tempdir() as d:
      d.create_file("foo.pyi", "def get_bar() -> bar.Bar: ...")
      d.create_file("bar.pyi", "class Bar:\n  pass")
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )

      loader.import_name("bar")
      d.delete_file("bar.pyi")

      foo = loader.import_name("foo")
      (f,) = foo.Lookup("foo.get_bar").signatures
      self.assertEqual("bar.Bar", f.return_type.cls.name)

  def test_remove(self):
    with test_utils.Tempdir() as d:
      d.create_file("foo.pyi", "def get_bar() -> bar.Bar: ...")
      d.create_file("bar.pyi", "class Bar:\n  pass")
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )

      bar = loader.import_name("bar")
      self.assertTrue(bar.Lookup("bar.Bar"))
      d.delete_file("bar.pyi")
      loader.remove_name("bar")

      with self.assertRaisesRegex(load_pytd.BadDependencyError, "bar"):
        loader.import_name("foo")

  def test_relative(self):
    with test_utils.Tempdir() as d:
      d.create_file("__init__.pyi", "base = ...  # type: str")
      d.create_file(
          file_utils.replace_separator("path/__init__.pyi"),
          "path = ...  # type: str",
      )
      d.create_file(
          file_utils.replace_separator("path/to/__init__.pyi"),
          "to = ...  # type: str",
      )
      d.create_file(
          file_utils.replace_separator("path/to/some/__init__.pyi"),
          "some = ...  # type: str",
      )
      d.create_file(file_utils.replace_separator("path/to/some/module.pyi"), "")
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="path.to.some.module",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      some = loader.import_relative(1)
      to = loader.import_relative(2)
      path = loader.import_relative(3)
      # Python doesn't allow "...." here, so don't test import_relative(4).
      self.assertTrue(some.Lookup("path.to.some.some"))
      self.assertTrue(to.Lookup("path.to.to"))
      self.assertTrue(path.Lookup("path.path"))

  def test_typeshed(self):
    with self._setup_loader() as loader:
      self.assertTrue(loader.import_name("urllib.request"))

  def test_prefer_typeshed(self):
    with test_utils.Tempdir() as d:
      # Override two modules from typeshed
      d.create_file(
          file_utils.replace_separator("typing_extensions/__init__.pyi"),
          "foo: str = ...",
      )
      d.create_file(
          file_utils.replace_separator("crypt/__init__.pyi"), "foo: str = ..."
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="x",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      # typing_extensions should ignore the override, crypt should not.
      ast1 = loader.import_name("typing_extensions")
      ast2 = loader.import_name("crypt")
      self.assertTrue(ast1.Lookup("typing_extensions.Literal"))
      self.assertTrue(ast2.Lookup("crypt.foo"))
      with self.assertRaises(KeyError):
        ast1.Lookup("typing_extensions.foo")
      with self.assertRaises(KeyError):
        ast2.Lookup("crypt.crypt")

  def test_resolve_alias(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          "module1.pyi",
          """
          from typing import List
          x = List[int]
      """,
      )
      d.create_file(
          "module2.pyi",
          """
          def f() -> module1.x: ...
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      module2 = loader.import_name("module2")
      (f,) = module2.Lookup("module2.f").signatures
      self.assertEqual("list[int]", pytd_utils.Print(f.return_type))

  def test_import_map_congruence(self):
    with test_utils.Tempdir() as d:
      foo_path = d.create_file("foo.pyi", "class X: ...")
      bar_path = d.create_file("bar.pyi", "X = ...  # type: another.foo.X")
      # Map the same pyi file under two module paths.
      null_device = "/dev/null" if sys.platform != "win32" else "NUL"
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath="",
          )
      )
      loader.options.tweak(
          imports_map=imports_map.ImportsMap(
              items={
                  "foo": foo_path,
                  file_utils.replace_separator("another/foo"): foo_path,
                  "bar": bar_path,
                  "empty1": null_device,
                  "empty2": null_device,
              }
          )
      )
      normal = loader.import_name("foo")
      self.assertEqual("foo", normal.name)
      loader.import_name("bar")  # check that we can resolve against another.foo
      another = loader.import_name("another.foo")
      # We do *not* treat foo.X and another.foo.X the same, because Python
      # doesn't, either:
      self.assertIsNot(normal, another)
      self.assertTrue([c.name.startswith("foo") for c in normal.classes])
      self.assertTrue(
          [c.name.startswith("another.foo") for c in another.classes]
      )
      # Make sure that multiple modules using /dev/null are not treated as
      # congruent.
      empty1 = loader.import_name("empty1")
      empty2 = loader.import_name("empty2")
      self.assertIsNot(empty1, empty2)
      self.assertEqual("empty1", empty1.name)
      self.assertEqual("empty2", empty2.name)

  def test_unused_imports_map_paths(self):
    with test_utils.Tempdir() as d:
      foo_path = d.create_file("foo.pyi", "class Foo: ...")
      bar_path = d.create_file("bar.pyi", "bar: foo.Foo = ...")
      baz_path = d.create_file("baz.pyi", "class Baz: ...")
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath="",
          )
      )
      loader.options.tweak(
          imports_map=imports_map.ImportsMap(
              items={
                  "foo": foo_path,
                  "bar": bar_path,
                  "baz": baz_path,
                  file_utils.replace_separator("aliased/baz"): baz_path,
              }
          )
      )
      self.assertEqual(
          {foo_path, bar_path, baz_path},
          loader.get_unused_imports_map_paths(),
      )

      _ = loader.import_name("bar")
      # Importing bar will access its upstream dependency foo.
      self.assertEqual(
          {baz_path},
          loader.get_unused_imports_map_paths(),
      )
      _ = loader.import_name("foo")
      self.assertEqual(
          {baz_path},
          loader.get_unused_imports_map_paths(),
      )
      _ = loader.import_name("aliased.baz")
      self.assertFalse(loader.get_unused_imports_map_paths())

  def test_package_relative_import(self):
    with test_utils.Tempdir() as d:
      d.create_file(file_utils.replace_separator("pkg/foo.pyi"), "class X: ...")
      d.create_file(
          file_utils.replace_separator("pkg/bar.pyi"),
          """
          from .foo import X
          y = ...  # type: X""",
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="pkg.bar",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      bar = loader.import_name("pkg.bar")
      f = bar.Lookup("pkg.bar.y")
      self.assertEqual("pkg.foo.X", f.type.name)

  def test_directory_import(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          file_utils.replace_separator("pkg/sub/__init__.pyi"),
          """
          from .foo import *
          from .bar import *""",
      )
      d.create_file(
          file_utils.replace_separator("pkg/sub/foo.pyi"),
          """
          class X: pass""",
      )
      d.create_file(
          file_utils.replace_separator("pkg/sub/bar.pyi"),
          """
          from .foo import X
          y = ...  # type: X""",
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="pkg",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      ast = loader.import_name("pkg.sub")
      self.assertTrue(ast.Lookup("pkg.sub.X"))

  def test_diamond_import(self):
    """Should not fail on importing a module via two paths."""
    with test_utils.Tempdir() as d:
      d.create_file(
          file_utils.replace_separator("pkg/sub/__init__.pyi"),
          """
          from .foo import *
          from .bar import *""",
      )
      d.create_file(
          file_utils.replace_separator("pkg/sub/foo.pyi"),
          """
          from .baz import X""",
      )
      d.create_file(
          file_utils.replace_separator("pkg/sub/bar.pyi"),
          """
          from .baz import X""",
      )
      d.create_file(
          file_utils.replace_separator("pkg/sub/baz.pyi"),
          """
          class X: ...""",
      )
      loader = load_pytd.Loader(
          config.Options.create(
              module_name="pkg",
              python_version=self.python_version,
              pythonpath=d.path,
          )
      )
      ast = loader.import_name("pkg.sub")
      self.assertTrue(ast.Lookup("pkg.sub.X"))

  def test_get_resolved_modules(self):
    with test_utils.Tempdir() as d:
      filename = d.create_file(
          file_utils.replace_separator("dir/module.pyi"),
          "def foo() -> str: ...",
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      ast = loader.import_name("dir.module")
      modules = loader.get_resolved_modules()
      self.assertEqual(set(modules), {"builtins", "typing", "dir.module"})
      module = modules["dir.module"]
      self.assertEqual(module.module_name, "dir.module")
      self.assertEqual(module.filename, filename)
      self.assertEqual(module.ast, ast)

  def test_circular_import(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          file_utils.replace_separator("os2/__init__.pyi"),
          """
        from . import path as path
        _PathType = path._PathType
        def utime(path: _PathType) -> None: ...
        class stat_result: ...
      """,
      )
      d.create_file(
          file_utils.replace_separator("os2/path.pyi"),
          """
        import os2
        _PathType = bytes
        def samestat(stat1: os2.stat_result) -> bool: ...
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      ast = loader.import_name("os2.path")
      self.assertEqual(
          ast.Lookup("os2.path._PathType").type.name, "builtins.bytes"
      )

  def test_circular_import_with_external_type(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          file_utils.replace_separator("os2/__init__.pyi"),
          """
        from posix2 import stat_result as stat_result
        from . import path as path
        _PathType = path._PathType
        def utime(path: _PathType) -> None: ...
      """,
      )
      d.create_file(
          file_utils.replace_separator("os2/path.pyi"),
          """
        import os2
        _PathType = bytes
        def samestate(stat1: os2.stat_result) -> bool: ...
      """,
      )
      d.create_file("posix2.pyi", "class stat_result: ...")
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      # Make sure all three modules were resolved properly.
      loader.import_name("os2")
      loader.import_name("os2.path")
      loader.import_name("posix2")

  def test_union_alias(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          "test.pyi",
          """
        from typing import Union as _UnionT
        x: _UnionT[int, str]
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      ast = loader.import_name("test")
      x = ast.Lookup("test.x")
      self.assertIsInstance(x.type, pytd.UnionType)

  def test_optional_alias(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          "test.pyi",
          """
        from typing import Optional as _OptionalT
        x: _OptionalT[int]
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      ast = loader.import_name("test")
      x = ast.Lookup("test.x")
      self.assertIsInstance(x.type, pytd.UnionType)

  def test_intersection_alias(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          "test.pyi",
          """
        from typing import Intersection as _IntersectionT
        x: _IntersectionT[int, str]
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      ast = loader.import_name("test")
      x = ast.Lookup("test.x")
      self.assertIsInstance(x.type, pytd.IntersectionType)

  def test_open_function(self):
    def mock_open(*unused_args, **unused_kwargs):
      return io.StringIO("x: int")

    loader = load_pytd.Loader(
        config.Options.create(
            module_name="base",
            python_version=self.python_version,
            open_function=mock_open,
        )
    )
    a = loader.load_file("a", "a.pyi")
    self.assertEqual("int", pytd_utils.Print(a.Lookup("a.x").type))

  def test_submodule_reexport(self):
    with test_utils.Tempdir() as d:
      d.create_file(file_utils.replace_separator("foo/bar.pyi"), "")
      d.create_file(
          file_utils.replace_separator("foo/__init__.pyi"),
          """
        from . import bar as bar
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      foo = loader.import_name("foo")
      self.assertEqual(pytd_utils.Print(foo), "import foo.bar")

  def test_submodule_rename(self):
    with test_utils.Tempdir() as d:
      d.create_file(file_utils.replace_separator("foo/bar.pyi"), "")
      d.create_file(
          file_utils.replace_separator("foo/__init__.pyi"),
          """
        from . import bar as baz
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      foo = loader.import_name("foo")
      self.assertEqual(pytd_utils.Print(foo), "from foo import bar as foo.baz")

  def test_typing_reexport(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          file_utils.replace_separator("foo.pyi"),
          """
        from typing import List as List
      """,
      )
      d.create_file(
          file_utils.replace_separator("bar.pyi"),
          """
        from foo import *
        def f() -> List[int]: ...
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      foo = loader.import_name("foo")
      bar = loader.import_name("bar")
      self.assertEqual(
          pytd_utils.Print(foo), "from builtins import list as List"
      )
      self.assertEqual(
          pytd_utils.Print(bar),
          textwrap.dedent("""
        from builtins import list as List

        def bar.f() -> list[int]: ...
      """).strip(),
      )

  def test_reuse_builtin_name(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          "foo.pyi",
          """
        class Ellipsis: ...
      """,
      )
      d.create_file(
          "bar.pyi",
          """
        from foo import *
        def f(x: Ellipsis): ...
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      loader.import_name("foo")
      bar = loader.import_name("bar")
      self.assertEqual(
          pytd_utils.Print(bar.Lookup("bar.f")),
          "def bar.f(x: foo.Ellipsis) -> Any: ...",
      )

  def test_import_typevar(self):
    # Regression test for the loader crashing with a
    # "Duplicate top level items: 'T', 'T'" error.
    self._import(
        a="""
      from typing import TypeVar
      T = TypeVar('T')
    """,
        b="""
      from a import T
      def f(x: T) -> T: ...
    """,
        c="""
      from b import *
    """,
    )

  def test_import_class_from_parent_module(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          file_utils.replace_separator("foo/__init__.pyi"), "class Foo: ..."
      )
      d.create_file(
          file_utils.replace_separator("foo/bar.pyi"),
          """
        from . import Foo
        class Bar(Foo): ...
      """,
      )
      loader = load_pytd.Loader(
          config.Options.create(
              python_version=self.python_version, pythonpath=d.path
          )
      )
      loader.import_name("foo.bar")

  def test_module_alias(self):
    ast = self._import(foo="""
      import subprocess as _subprocess
      x: _subprocess.Popen
    """)
    expected = textwrap.dedent("""
      import subprocess as foo._subprocess

      foo.x: foo._subprocess.Popen
    """).strip()
    self.assertMultiLineEqual(pytd_utils.Print(ast), expected)

  def test_star_import_in_circular_dep(self):
    stub3_ast = self._import(
        stub1="""
      from stub2 import Foo
      from typing import Mapping as Mapping
    """,
        stub2="""
      from stub3 import Mapping
      class Foo: ...
    """,
        stub3="""
      from stub1 import *
    """,
    )
    self.assertEqual(
        stub3_ast.Lookup("stub3.Foo").type, pytd.ClassType("stub2.Foo")
    )
    self.assertEqual(
        stub3_ast.Lookup("stub3.Mapping").type, pytd.ClassType("typing.Mapping")
    )

  def test_import_all(self):
    ast = self._import(
        foo="__all__ = ['foo']",
        bar="__all__ = ['bar']",
        baz="""
      from foo import *
      from bar import *
    """,
    )
    self.assertFalse(ast.aliases)

  def test_import_private_typevar(self):
    ast = self._import(
        foo="""
      from typing import TypeVar
      _T = TypeVar('_T')
    """,
        bar="""
      from typing import TypeVar
      _T = TypeVar('_T')
    """,
        baz="""
      from foo import *
      from bar import *
    """,
    )
    self.assertFalse(ast.type_params)

  def test_use_class_alias(self):
    ast = self._import(foo="""
      class A:
        class B: ...
        x: A2.B
      A2 = A
    """)
    a = ast.Lookup("foo.A")
    self.assertEqual(a.Lookup("x").type.cls, a.Lookup("foo.A.B"))

  def test_alias_typevar(self):
    ast = self._import(foo="""
      from typing import TypeVar as _TypeVar
      T = _TypeVar('T')
    """)
    self.assertEqual(
        ast.Lookup("foo.T"), pytd.TypeParameter(name="T", scope="foo")
    )

  def test_alias_property_with_setter(self):
    ast = self._import(foo="""
      class X:
        @property
        def f(self) -> int: ...
        @f.setter
        def f(self, value: int) -> None: ...
        g = f
    """)
    x = ast.Lookup("foo.X")
    self.assertEqual(
        pytd_utils.Print(x.Lookup("f")), "f: Annotated[int, 'property']"
    )
    self.assertEqual(
        pytd_utils.Print(x.Lookup("g")), "g: Annotated[int, 'property']"
    )

  def test_typing_alias(self):
    # typing._Alias is a typeshed construct.
    ast = self._import(foo="""
      from typing import _Alias, TypeAlias
      X = _Alias()
      Y: TypeAlias = _Alias()
    """)
    self.assertEqual(
        pytd_utils.Print(ast), "from typing import _Alias as X, _Alias as Y"
    )


class ImportTypeMacroTest(_LoaderTest):

  def test_container(self):
    ast = self._import(
        a="""
      from typing import List, TypeVar
      T = TypeVar('T')
      Alias = List[T]
    """,
        b="""
      import a
      Strings = a.Alias[str]
    """,
    )
    self.assertEqual(
        pytd_utils.Print(ast.Lookup("b.Strings").type), "list[str]"
    )

  def test_union(self):
    ast = self._import(
        a="""
      from typing import List, TypeVar, Union
      T = TypeVar('T')
      Alias = Union[T, List[T]]
    """,
        b="""
      import a
      Strings = a.Alias[str]
    """,
    )
    self.assertEqual(
        pytd_utils.Print(ast.Lookup("b.Strings").type), "Union[str, list[str]]"
    )

  def test_bad_parameterization(self):
    with self.assertRaisesRegex(
        load_pytd.BadDependencyError,
        r"Union\[T, list\[T\]\] expected 1 parameters, got 2",
    ):
      self._import(
          a="""
        from typing import List, TypeVar, Union
        T = TypeVar('T')
        Alias = Union[T, List[T]]
      """,
          b="""
        import a
        Strings = a.Alias[str, str]
      """,
      )

  def test_parameterize_twice(self):
    ast = self._import(
        a="""
      from typing import AnyStr, Generic
      class Foo(Generic[AnyStr]): ...
    """,
        b="""
      import a
      from typing import AnyStr
      x: Foo[str]
      Foo = a.Foo[AnyStr]
    """,
    )
    self.assertEqual(pytd_utils.Print(ast.Lookup("b.x").type), "a.Foo[str]")


@dataclasses.dataclass(eq=True, frozen=True)
class _Module:
  module_name: str
  file_name: str


class PickledPyiLoaderTest(test_base.UnitTest):

  def _create_files(self, tempdir):
    src = """
        import module2
        from typing import List

        constant = True

        x = List[int]
        b = List[int]

        class SomeClass:
          def __init__(self, a: module2.ObjectMod2):
            pass

        def ModuleFunction():
          pass
    """
    tempdir.create_file("module1.pyi", src)
    tempdir.create_file(
        "module2.pyi",
        """
        class ObjectMod2:
          def __init__(self):
            pass
    """,
    )

  def _get_path(self, tempdir, filename):
    return path_utils.join(tempdir.path, filename)

  def _load_ast(self, tempdir, module):
    loader = load_pytd.Loader(
        config.Options.create(
            module_name=module.module_name,
            python_version=self.python_version,
            pythonpath=tempdir.path,
        )
    )
    return loader, loader.load_file(
        module.module_name, self._get_path(tempdir, module.file_name)
    )

  def _pickle_modules(self, loader, tempdir, *modules):
    for module in modules:
      pickle_utils.SerializeAndSave(
          loader._modules[module.module_name].ast,
          self._get_path(tempdir, module.file_name + ".pickled"),
      )

  def _load_pickled_module(self, tempdir, module):
    pickle_loader = load_pytd.PickledPyiLoader(
        config.Options.create(
            python_version=self.python_version, pythonpath=tempdir.path
        )
    )
    return pickle_loader.load_file(
        module.module_name, self._get_path(tempdir, module.file_name)
    )

  def test_load_with_same_module_name(self):
    with test_utils.Tempdir() as d:
      self._create_files(tempdir=d)
      module1 = _Module(module_name="foo.bar.module1", file_name="module1.pyi")
      module2 = _Module(module_name="module2", file_name="module2.pyi")
      loader, ast = self._load_ast(tempdir=d, module=module1)
      self._pickle_modules(loader, d, module1, module2)
      pickled_ast_filename = self._get_path(d, module1.file_name + ".pickled")
      result = pickle_utils.SerializeAndSave(ast, pickled_ast_filename)
      self.assertIsNone(result)

      loaded_ast = self._load_pickled_module(d, module1)
      self.assertTrue(loaded_ast)
      self.assertIsNot(loaded_ast, ast)
      self.assertTrue(pytd_utils.ASTeq(ast, loaded_ast))
      loaded_ast.Visit(visitors.VerifyLookup())

  def test_star_import(self):
    with test_utils.Tempdir() as d:
      d.create_file("foo.pyi", "class A: ...")
      d.create_file("bar.pyi", "from foo import *")
      foo = _Module(module_name="foo", file_name="foo.pyi")
      bar = _Module(module_name="bar", file_name="bar.pyi")
      loader, _ = self._load_ast(d, module=bar)
      self._pickle_modules(loader, d, foo, bar)
      loaded_ast = self._load_pickled_module(d, bar)
      loaded_ast.Visit(visitors.VerifyLookup())
      self.assertEqual(pytd_utils.Print(loaded_ast), "from foo import A")

  def test_function_alias(self):
    with test_utils.Tempdir() as d:
      d.create_file(
          "foo.pyi",
          """
        def f(): ...
        g = f
      """,
      )
      foo = _Module(module_name="foo", file_name="foo.pyi")
      loader, _ = self._load_ast(d, module=foo)
      self._pickle_modules(loader, d, foo)
      loaded_ast = self._load_pickled_module(d, foo)
      g = loaded_ast.Lookup("foo.g")
      self.assertEqual(g.type, loaded_ast.Lookup("foo.f"))

  def test_package_relative_import(self):
    with test_utils.Tempdir() as d:
      d.create_file(file_utils.replace_separator("pkg/foo.pyi"), "class X: ...")
      d.create_file(
          file_utils.replace_separator("pkg/bar.pyi"),
          """
          from .foo import X
          y = ...  # type: X""",
      )
      foo = _Module(
          module_name="pkg.foo",
          file_name=file_utils.replace_separator("pkg/foo.pyi"),
      )
      bar = _Module(
          module_name="pkg.bar",
          file_name=file_utils.replace_separator("pkg/bar.pyi"),
      )
      loader, _ = self._load_ast(d, module=bar)
      self._pickle_modules(loader, d, foo, bar)
      loaded_ast = self._load_pickled_module(d, bar)
      loaded_ast.Visit(visitors.VerifyLookup())

  def test_pickled_builtins(self):
    with test_utils.Tempdir() as d:
      filename = d.create_file("builtins.pickle")
      foo_path = d.create_file(
          "foo.pickle",
          """
        import datetime
        tz = ...  # type: datetime.tzinfo
      """,
      )
      # save builtins
      load_pytd.Loader(
          config.Options.create(
              module_name="base", python_version=self.python_version
          )
      ).save_to_pickle(filename)
      # load builtins
      loader = load_pytd.PickledPyiLoader.load_from_pickle(
          filename,
          config.Options.create(
              module_name="base",
              python_version=self.python_version,
              pythonpath="",
          ),
      )
      loader.options.tweak(
          imports_map=imports_map.ImportsMap(items={"foo": foo_path})
      )
      # test import
      self.assertTrue(loader.import_name("sys"))
      self.assertTrue(loader.import_name("__future__"))
      self.assertTrue(loader.import_name("datetime"))
      self.assertTrue(loader.import_name("foo"))
      self.assertTrue(loader.import_name("ctypes"))


class MethodAliasTest(_LoaderTest):

  def test_import_class(self):
    b_ast = self._import(
        a="""
      class Foo:
        def f(self) -> int: ...
    """,
        b="""
      import a
      f = a.Foo.f
    """,
    )
    self.assertEqual(
        pytd_utils.Print(b_ast.Lookup("b.f")),
        "def b.f(self: a.Foo) -> int: ...",
    )

  def test_import_class_instance(self):
    b_ast = self._import(
        a="""
      class Foo:
        def f(self) -> int: ...
      foo: Foo
    """,
        b="""
      import a
      f = a.foo.f
    """,
    )
    self.assertEqual(
        pytd_utils.Print(b_ast.Lookup("b.f")), "def b.f() -> int: ..."
    )

  def test_create_instance_after_import(self):
    b_ast = self._import(
        a="""
      class Foo:
        def f(self) -> int: ...
    """,
        b="""
      import a
      foo: a.Foo
      f = foo.f
    """,
    )
    self.assertEqual(
        pytd_utils.Print(b_ast.Lookup("b.f")), "def b.f() -> int: ..."
    )

  def test_function(self):
    ast = self._import(a="""
      def f(x: int) -> int: ...
      g = f
    """)
    self.assertEqual(
        pytd_utils.Print(ast.Lookup("a.g")), "def a.g(x: int) -> int: ..."
    )

  def test_imported_function(self):
    b_ast = self._import(
        a="""
      def f(x: int) -> int: ...
    """,
        b="""
      import a
      f = a.f
    """,
    )
    self.assertEqual(
        pytd_utils.Print(b_ast.Lookup("b.f")), "def b.f(x: int) -> int: ..."
    )

  def test_base_class(self):
    a_ast = self._import(a="""
      class Foo:
        def f(self) -> int: ...
      class Bar(Foo): ...
      x: Bar
      f = x.f
    """)
    self.assertEqual(
        pytd_utils.Print(a_ast.Lookup("a.f")), "def a.f() -> int: ..."
    )

  def test_base_class_imported(self):
    b_ast = self._import(
        a="""
      class Foo:
        def f(self) -> int: ...
      class Bar(Foo): ...
      x: Bar
    """,
        b="""
      import a
      f = a.x.f
    """,
    )
    self.assertEqual(
        pytd_utils.Print(b_ast.Lookup("b.f")), "def b.f() -> int: ..."
    )


class RecursiveAliasTest(_LoaderTest):

  def test_basic(self):
    ast = self._import(a="""
      from typing import List
      X = List[X]
    """)
    actual_x = ast.Lookup("a.X")
    expected_x = pytd.Alias(
        name="a.X",
        type=pytd.GenericType(
            base_type=pytd.ClassType("builtins.list"),
            parameters=(pytd.LateType("a.X", recursive=True),),
        ),
    )
    self.assertEqual(actual_x, expected_x)

  def test_mutual_recursion(self):
    ast = self._import(a="""
      from typing import List
      X = List[Y]
      Y = List[X]
    """)

    actual_x = ast.Lookup("a.X")
    expected_x = pytd.Alias(
        name="a.X",
        type=pytd.GenericType(
            base_type=pytd.ClassType("builtins.list"),
            parameters=(pytd.LateType("a.Y", recursive=True),),
        ),
    )
    self.assertEqual(actual_x, expected_x)

    actual_y = ast.Lookup("a.Y")
    expected_y = pytd.Alias(
        name="a.Y",
        type=pytd.GenericType(
            base_type=pytd.ClassType("builtins.list"),
            parameters=(
                pytd.GenericType(
                    base_type=pytd.ClassType("builtins.list"),
                    parameters=(pytd.LateType("a.Y", recursive=True),),
                ),
            ),
        ),
    )
    self.assertEqual(actual_y, expected_y)

  def test_very_mutual_recursion(self):
    ast = self._import(a="""
      from typing import List
      X = List[Y]
      Y = List[Z]
      Z = List[X]
    """)

    actual_x = ast.Lookup("a.X")
    expected_x = pytd.Alias(
        name="a.X",
        type=pytd.GenericType(
            base_type=pytd.ClassType("builtins.list"),
            parameters=(pytd.LateType("a.Y", recursive=True),),
        ),
    )
    self.assertEqual(actual_x, expected_x)

    actual_y = ast.Lookup("a.Y")
    expected_y = pytd.Alias(
        name="a.Y",
        type=pytd.GenericType(
            base_type=pytd.ClassType("builtins.list"),
            parameters=(pytd.LateType("a.Z", recursive=True),),
        ),
    )
    self.assertEqual(actual_y, expected_y)

    actual_z = ast.Lookup("a.Z")
    expected_z = pytd.Alias(
        name="a.Z",
        type=pytd.GenericType(
            base_type=pytd.ClassType("builtins.list"),
            parameters=(
                pytd.GenericType(
                    base_type=pytd.ClassType("builtins.list"),
                    parameters=(pytd.LateType("a.Y", recursive=True),),
                ),
            ),
        ),
    )
    self.assertEqual(actual_z, expected_z)


class NestedClassTest(_LoaderTest):

  def test_basic(self):
    ast = self._import(a="""
      class A:
        class B:
          def f(self) -> C: ...
        class C(B): ...
    """)
    actual_f = pytd.LookupItemRecursive(ast, "A.B.f")
    self.assertEqual(
        pytd_utils.Print(actual_f), "def f(self: a.A.B) -> a.A.C: ..."
    )
    actual_c = pytd.LookupItemRecursive(ast, "A.C")
    self.assertEqual(
        pytd_utils.Print(actual_c).rstrip(), "class a.A.C(a.A.B): ..."
    )

  @test_base.skip("This does not work yet")
  def test_shadowing(self):
    ast = self._import(a="""
      class A:
        class A(A):
          def f(self) -> A: ...
        class C(A): ...
    """)
    self.assertEqual(
        pytd_utils.Print(ast).rstrip(),
        textwrap.dedent("""
      class a.A:
          class a.A.A(a.A):
              def f(self) -> a.A.A: ...
          class a.A.C(a.A.A): ...
      """).strip(),
    )


if __name__ == "__main__":
  unittest.main()
