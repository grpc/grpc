import re
import textwrap

from pytype import config
from pytype import load_pytd
from pytype.pytd import optimize
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.pytd.parse import parser_test_base

import unittest


def pytd_src(text):
  """Add a typing.Union import if needed."""
  text = textwrap.dedent(text)
  if "Union" in text and not re.search("typing.*Union", text):
    return "from typing import Union\n" + text
  else:
    return text


class TestOptimize(parser_test_base.ParserTest):
  """Test the visitors in optimize.py."""

  @classmethod
  def setUpClass(cls):
    super().setUpClass()
    cls.loader = load_pytd.Loader(
        config.Options.create(python_version=cls.python_version)
    )
    cls.builtins = cls.loader.builtins
    cls.typing = cls.loader.typing

  def ParseAndResolve(self, src):
    ast = self.Parse(src)
    return ast.Visit(visitors.LookupBuiltins(self.builtins))

  def Optimize(self, ast, **kwargs):
    return optimize.Optimize(ast, self.builtins, **kwargs)

  def OptimizedString(self, data):
    tree = self.Parse(data) if isinstance(data, str) else data
    new_tree = self.Optimize(tree)
    return pytd_utils.Print(new_tree)

  def AssertOptimizeEquals(self, src, new_src):
    self.AssertSourceEquals(self.OptimizedString(src), new_src)

  def test_one_function(self):
    src = pytd_src("""
        def foo(a: int, c: bool) -> int:
          raise AssertionError()
          raise ValueError()
    """)
    self.AssertOptimizeEquals(src, src)

  def test_function_duplicate(self):
    src = pytd_src("""
        def foo(a: int, c: bool) -> int:
          raise AssertionError()
          raise ValueError()
        def foo(a: int, c: bool) -> int:
          raise AssertionError()
          raise ValueError()
    """)
    new_src = pytd_src("""
        def foo(a: int, c: bool) -> int:
          raise AssertionError()
          raise ValueError()
    """)
    self.AssertOptimizeEquals(src, new_src)

  def test_complex_function_duplicate(self):
    src = pytd_src("""
        def foo(a: Union[int, float], c: bool) -> list[int]:
          raise IndexError()
        def foo(a: str, c: str) -> str: ...
        def foo(a: int, *args) -> Union[int, float]:
          raise ValueError()
        def foo(a: Union[int, float], c: bool) -> list[int]:
          raise IndexError()
        def foo(a: int, *args) -> Union[int, float]:
          raise ValueError()
    """)
    new_src = pytd_src("""
        def foo(a: float, c: bool) -> list[int]:
          raise IndexError()
        def foo(a: str, c: str) -> str: ...
        def foo(a: int, *args) -> Union[int, float]:
          raise ValueError()
    """)
    self.AssertOptimizeEquals(src, new_src)

  def test_combine_returns(self):
    src = pytd_src("""
        def foo(a: int) -> int: ...
        def foo(a: int) -> float: ...
    """)
    new_src = pytd_src("""
        def foo(a: int) -> Union[int, float]: ...
    """)
    self.AssertOptimizeEquals(src, new_src)

  def test_combine_redundant_returns(self):
    src = pytd_src("""
        def foo(a: int) -> int: ...
        def foo(a: int) -> float: ...
        def foo(a: int) -> Union[int, float]: ...
    """)
    new_src = pytd_src("""
        def foo(a: int) -> Union[int, float]: ...
    """)
    self.AssertOptimizeEquals(src, new_src)

  def test_combine_union_returns(self):
    src = pytd_src("""
        def foo(a: int) -> Union[int, float]: ...
        def bar(a: str) -> str: ...
        def foo(a: int) -> Union[str, bytes]: ...
    """)
    new_src = pytd_src("""
        def foo(a: int) -> Union[int, float, str, bytes]: ...
        def bar(a: str) -> str: ...
    """)
    self.AssertOptimizeEquals(src, new_src)

  def test_combine_exceptions(self):
    src = pytd_src("""
        def foo(a: int) -> int:
          raise ValueError()
        def foo(a: int) -> int:
          raise IndexError()
        def foo(a: float) -> int:
          raise IndexError()
        def foo(a: int) -> int:
          raise AttributeError()
    """)
    new_src = pytd_src("""
        def foo(a: int) -> int:
          raise ValueError()
          raise IndexError()
          raise AttributeError()
        def foo(a: float) -> int:
          raise IndexError()
    """)
    self.AssertOptimizeEquals(src, new_src)

  def test_mixed_combine(self):
    src = pytd_src("""
        def foo(a: int) -> int:
          raise ValueError()
        def foo(a: int) -> float:
          raise ValueError()
        def foo(a: int) -> int:
          raise IndexError()
    """)
    new_src = pytd_src("""
        def foo(a: int) -> Union[int, float]:
          raise ValueError()
          raise IndexError()
    """)
    self.AssertOptimizeEquals(src, new_src)

  def test_lossy(self):
    # Lossy compression is hard to test, since we don't know to which degree
    # "compressible" items will be compressed. This test only checks that
    # non-compressible things stay the same.
    src = pytd_src("""
        def foo(a: int) -> float:
          raise IndexError()
        def foo(a: str) -> complex:
          raise AssertionError()
    """)
    optimized = self.Optimize(self.Parse(src), lossy=True, use_abcs=False)
    self.AssertSourceEquals(optimized, src)

  @unittest.skip("Needs ABCs to be included in the builtins")
  def test_abcs(self):
    src = pytd_src("""
        def foo(a: Union[int, float]) -> NoneType: ...
        def foo(a: Union[int, complex, float]) -> NoneType: ...
    """)
    new_src = pytd_src("""
        def foo(a: Real) -> NoneType: ...
        def foo(a: Complex) -> NoneType: ...
    """)
    optimized = self.Optimize(self.Parse(src), lossy=True, use_abcs=True)
    self.AssertSourceEquals(optimized, new_src)

  def test_duplicates_in_unions(self):
    src = pytd_src("""
      def a(x: Union[int, float, complex]) -> bool: ...
      def b(x: Union[int, float]) -> bool: ...
      def c(x: Union[int, int, int]) -> bool: ...
      def d(x: Union[int, int]) -> bool: ...
      def e(x: Union[float, int, int, float]) -> bool: ...
      def f(x: Union[float, int]) -> bool: ...
    """)
    new_src = pytd_src("""
      def a(x) -> builtins.bool: ...  # max_union=2 makes this object
      def b(x: Union[builtins.int, builtins.float]) -> builtins.bool: ...
      def c(x: builtins.int) -> builtins.bool: ...
      def d(x: builtins.int) -> builtins.bool: ...
      def e(x: Union[builtins.float, builtins.int]) -> builtins.bool: ...
      def f(x: Union[builtins.float, builtins.int]) -> builtins.bool: ...
    """)
    ast = self.ParseAndResolve(src)
    optimized = self.Optimize(ast, lossy=False, max_union=2)
    self.AssertSourceEquals(optimized, new_src)

  def test_simplify_unions(self):
    src = pytd_src("""
      from typing import Any
      a = ...  # type: Union[int, int]
      b = ...  # type: Union[int, Any]
      c = ...  # type: Union[int, int, float]
    """)
    new_src = pytd_src("""
      from typing import Any
      a = ...  # type: int
      b = ...  # type: Any
      c = ...  # type: Union[int, float]
    """)
    self.AssertSourceEquals(
        self.ApplyVisitorToString(src, optimize.SimplifyUnions()), new_src
    )

  def test_builtin_superclasses(self):
    src = pytd_src("""
        def f(x: Union[list, object], y: Union[complex, slice]) -> Union[int, bool]: ...
    """)
    expected = pytd_src("""
        def f(x: builtins.object, y: builtins.object) -> builtins.int: ...
    """)
    hierarchy = self.builtins.Visit(visitors.ExtractSuperClassesByName())
    hierarchy.update(self.typing.Visit(visitors.ExtractSuperClassesByName()))
    visitor = optimize.FindCommonSuperClasses(
        optimize.SuperClassHierarchy(hierarchy)
    )
    ast = self.ParseAndResolve(src)
    ast = ast.Visit(visitor)
    ast = ast.Visit(visitors.CanonicalOrderingVisitor())
    self.AssertSourceEquals(ast, expected)

  def test_user_superclass_hierarchy(self):
    class_data = pytd_src("""
        class AB:
            pass

        class EFG:
            pass

        class A(AB, EFG):
            pass

        class B(AB):
            pass

        class E(EFG, AB):
            pass

        class F(EFG):
            pass

        class G(EFG):
            pass
    """)

    src = pytd_src("""
        from typing import Any
        def f(x: Union[A, B], y: A, z: B) -> Union[E, F, G]: ...
        def g(x: Union[E, F, G, B]) -> Union[E, F]: ...
        def h(x) -> Any: ...
    """) + class_data

    expected = pytd_src("""
        from typing import Any
        def f(x: AB, y: A, z: B) -> EFG: ...
        def g(x: object) -> EFG: ...
        def h(x) -> Any: ...
    """) + class_data

    hierarchy = self.Parse(src).Visit(visitors.ExtractSuperClassesByName())
    visitor = optimize.FindCommonSuperClasses(
        optimize.SuperClassHierarchy(hierarchy)
    )
    new_src = self.ApplyVisitorToString(src, visitor)
    self.AssertSourceEquals(new_src, expected)

  def test_find_common_superclasses(self):
    src = pytd_src("""
        x = ...  # type: Union[int, other.Bar]
    """)
    expected = pytd_src("""
        x = ...  # type: Union[int, other.Bar]
    """)
    ast = self.Parse(src)
    ast = ast.Visit(
        visitors.ReplaceTypesByName({"other.Bar": pytd.LateType("other.Bar")})
    )
    hierarchy = ast.Visit(visitors.ExtractSuperClassesByName())
    ast = ast.Visit(
        optimize.FindCommonSuperClasses(optimize.SuperClassHierarchy(hierarchy))
    )
    ast = ast.Visit(visitors.LateTypeToClassType())
    self.AssertSourceEquals(ast, expected)

  def test_simplify_unions_with_superclasses(self):
    src = pytd_src("""
        x = ...  # type: Union[int, bool]
        y = ...  # type: Union[int, bool, float]
        z = ...  # type: Union[list[int], int]
    """)
    expected = pytd_src("""
        x = ...  # type: int
        y = ...  # type: Union[int, float]
        z = ...  # type: Union[list[int], int]
    """)
    hierarchy = self.builtins.Visit(visitors.ExtractSuperClassesByName())
    visitor = optimize.SimplifyUnionsWithSuperclasses(
        optimize.SuperClassHierarchy(hierarchy)
    )
    ast = self.Parse(src)
    ast = visitors.LookupClasses(ast, self.builtins)
    ast = ast.Visit(visitor)
    self.AssertSourceEquals(ast, expected)

  @unittest.skip("Needs better handling of GenericType")
  def test_simplify_unions_with_superclasses_generic(self):
    src = pytd_src("""
        x = ...  # type: Union[frozenset[int], AbstractSet[int]]
    """)
    expected = pytd_src("""
        x = ...  # type: AbstractSet[int]
    """)
    hierarchy = self.builtins.Visit(visitors.ExtractSuperClassesByName())
    visitor = optimize.SimplifyUnionsWithSuperclasses(
        optimize.SuperClassHierarchy(hierarchy)
    )
    ast = self.Parse(src)
    ast = visitors.LookupClasses(ast, self.builtins)
    ast = ast.Visit(visitor)
    self.AssertSourceEquals(ast, expected)

  def test_collapse_long_unions(self):
    src = pytd_src("""
        from typing import Any
        def f(x: Union[A, B, C, D]) -> X: ...
        def g(x: Union[A, B, C, D, E]) -> X: ...
        def h(x: Union[A, Any]) -> X: ...
    """)
    expected = pytd_src("""
        def f(x: Union[A, B, C, D]) -> X: ...
        def g(x) -> X: ...
        def h(x) -> X: ...
    """)
    ast = self.ParseAndResolve(src)
    ast = ast.Visit(optimize.CollapseLongUnions(max_length=4))
    self.AssertSourceEquals(ast, expected)

  def test_collapse_long_constant_unions(self):
    src = pytd_src("""
      x = ...  # type: Union[A, B, C, D]
      y = ...  # type: Union[A, B, C, D, E]
    """)
    expected = pytd_src("""
      from typing import Any
      x = ...  # type: Union[A, B, C, D]
      y = ...  # type: Any
    """)
    ast = self.ParseAndResolve(src)
    ast = ast.Visit(optimize.CollapseLongUnions(max_length=4))
    ast = ast.Visit(optimize.AdjustReturnAndConstantGenericType())
    self.AssertSourceEquals(ast, expected)

  def test_combine_containers(self):
    src = pytd_src("""
        from typing import Any
        def f(x: Union[list[int], list[float]]) -> Any: ...
        def g(x: Union[list[int], str, list[float], set[int], long]) -> Any: ...
        def h(x: Union[list[int], list[str], set[int], set[float]]) -> Any: ...
        def i(x: Union[list[int], list[int]]) -> Any: ...
        def j(x: Union[dict[int, float], dict[float, int]]) -> Any: ...
        def k(x: Union[dict[int, bool], list[int], dict[bool, int], list[bool]]) -> Any: ...
    """)
    expected = pytd_src("""
        from typing import Any
        def f(x: list[float]) -> Any: ...
        def g(x: Union[list[float], str, set[int], long]) -> Any: ...
        def h(x: Union[list[Union[int, str]], set[float]]) -> Any: ...
        def i(x: list[int]) -> Any: ...
        def j(x: dict[float, float]) -> Any: ...
        def k(x: Union[dict[Union[int, bool], Union[bool, int]], list[Union[int, bool]]]) -> Any: ...
    """)
    new_src = self.ApplyVisitorToString(src, optimize.CombineContainers())
    self.AssertSourceEquals(new_src, expected)

  def test_combine_containers_multi_level(self):
    src = pytd_src("""
      v = ...  # type: Union[list[tuple[Union[long, int], ...]], list[tuple[Union[float, bool], ...]]]
    """)
    expected = pytd_src("""
      v = ...  # type: list[tuple[Union[long, int, float, bool], ...]]
    """)
    new_src = self.ApplyVisitorToString(src, optimize.CombineContainers())
    self.AssertSourceEquals(new_src, expected)

  def test_combine_same_length_tuples(self):
    src = pytd_src("""
      x = ...  # type: Union[tuple[int], tuple[str]]
    """)
    expected = pytd_src("""
      x = ...  # type: tuple[Union[int, str]]
    """)
    new_src = self.ApplyVisitorToString(src, optimize.CombineContainers())
    self.AssertSourceEquals(new_src, expected)

  def test_combine_different_length_tuples(self):
    src = pytd_src("""
      x = ...  # type: Union[tuple[int], tuple[int, str]]
    """)
    expected = pytd_src("""
      x = ...  # type: tuple[Union[int, str], ...]
    """)
    new_src = self.ApplyVisitorToString(src, optimize.CombineContainers())
    self.AssertSourceEquals(new_src, expected)

  def test_combine_different_length_callables(self):
    src = pytd_src("""
      from typing import Callable
      x = ...  # type: Union[Callable[[int], str], Callable[[int, int], str]]
    """)
    expected = pytd_src("""
      from typing import Callable
      x = ...  # type: Callable[..., str]
    """)
    new_src = self.ApplyVisitorToString(src, optimize.CombineContainers())
    self.AssertSourceEquals(new_src, expected)

  def test_pull_in_method_classes(self):
    src = pytd_src("""
        from typing import Any
        class A:
            mymethod1 = ...  # type: Method1
            mymethod2 = ...  # type: Method2
            member = ...  # type: Method3
            mymethod4 = ...  # type: Method4
        class Method1:
            def __call__(self: A, x: int) -> Any: ...
        class Method2:
            def __call__(self: object, x: int) -> Any: ...
        class Method3:
            def __call__(x: bool, y: int) -> Any: ...
        class Method4:
            def __call__(self: Any) -> Any: ...
        class B(Method4):
            pass
    """)
    expected = pytd_src("""
        from typing import Any
        class A:
            member = ...  # type: Method3
            def mymethod1(self, x: int) -> Any: ...
            def mymethod2(self, x: int) -> Any: ...
            def mymethod4(self) -> Any: ...

        class Method3:
            def __call__(x: bool, y: int) -> Any: ...

        class Method4:
            def __call__(self) -> Any: ...

        class B(Method4):
            pass
    """)
    new_src = self.ApplyVisitorToString(src, optimize.PullInMethodClasses())
    self.AssertSourceEquals(new_src, expected)

  def test_add_inherited_methods(self):
    src = pytd_src("""
        from typing import Any
        class A():
            foo = ...  # type: bool
            def f(self, x: int) -> float: ...
            def h(self) -> complex: ...

        class B(A):
            bar = ...  # type: int
            def g(self, y: int) -> bool: ...
            def h(self, z: float) -> Any: ...
    """)
    ast = self.Parse(src)
    ast = visitors.LookupClasses(ast, self.builtins)
    self.assertCountEqual(("g", "h"), [m.name for m in ast.Lookup("B").methods])
    ast = ast.Visit(optimize.AddInheritedMethods())
    self.assertCountEqual(
        ("f", "g", "h"), [m.name for m in ast.Lookup("B").methods]
    )

  def test_adjust_inherited_method_self(self):
    src = pytd_src("""
      class A():
        def f(self: object) -> float: ...
      class B(A):
        pass
    """)
    ast = self.Parse(src)
    ast = visitors.LookupClasses(ast, self.builtins)
    ast = ast.Visit(optimize.AddInheritedMethods())
    self.assertMultiLineEqual(
        pytd_utils.Print(ast.Lookup("B")),
        pytd_src("""
        class B(A):
            def f(self) -> float: ...
    """).lstrip(),
    )

  def test_absorb_mutable_parameters(self):
    src = pytd_src("""
        from typing import Any
        def popall(x: list[Any]) -> Any:
            x = list[nothing]
        def add_float(x: list[int]) -> Any:
            x = list[Union[int, float]]
        def f(x: list[int]) -> Any:
            x = list[Union[int, float]]
    """)
    expected = pytd_src("""
        from typing import Any
        def popall(x: list[Any]) -> Any: ...
        def add_float(x: list[Union[int, float]]) -> Any: ...
        def f(x: list[Union[int, float]]) -> Any: ...
    """)
    tree = self.Parse(src)
    new_tree = tree.Visit(optimize.AbsorbMutableParameters())
    new_tree = new_tree.Visit(optimize.CombineContainers())
    self.AssertSourceEquals(new_tree, expected)

  def test_absorb_mutable_parameters_from_methods(self):
    # This is a test for intermediate data. See AbsorbMutableParameters class
    # pydoc about how AbsorbMutableParameters works on methods.
    src = pytd_src("""
        from typing import Any
        T = TypeVar('T')
        NEW = TypeVar('NEW')
        class MyClass(typing.Generic[T], object):
            def append(self, x: NEW) -> Any:
                self = MyClass[Union[T, NEW]]
    """)
    tree = self.Parse(src)
    new_tree = tree.Visit(optimize.AbsorbMutableParameters())
    new_tree = new_tree.Visit(optimize.CombineContainers())
    self_type = (
        new_tree.Lookup("MyClass").Lookup("append").signatures[0].params[0].type
    )
    self.assertEqual(pytd_utils.Print(self_type), "MyClass[Union[T, NEW]]")

  def test_merge_type_parameters(self):
    # This test uses pytd of the kind that's typically the output of
    # AbsorbMutableParameters.
    # See comment in RemoveMutableParameters
    src = pytd_src("""
      from typing import Any
      T = TypeVar('T')
      T2 = TypeVar('T2')
      T3 = TypeVar('T3')
      class A(typing.Generic[T], object):
          def foo(self, x: Union[T, T2]) -> T2: ...
          def bar(self, x: Union[T, T2, T3]) -> T3: ...
          def baz(self, x: Union[T, T2], y: Union[T2, T3]) -> Any: ...

      K = TypeVar('K')
      V = TypeVar('V')
      class D(typing.Generic[K, V], object):
          def foo(self, x: T) -> Union[K, T]: ...
          def bar(self, x: T) -> Union[V, T]: ...
          def baz(self, x: Union[K, V]) -> Union[K, V]: ...
          def lorem(self, x: T) -> Union[T, K, V]: ...
          def ipsum(self, x: T) -> Union[T, K]: ...
    """)
    expected = pytd_src("""
      from typing import Any
      T = TypeVar('T')
      T2 = TypeVar('T2')
      T3 = TypeVar('T3')
      class A(typing.Generic[T], object):
          def foo(self, x: T) -> T: ...
          def bar(self, x: T) -> T: ...
          def baz(self, x: T, y: T) -> Any: ...

      K = TypeVar('K')
      V = TypeVar('V')
      class D(typing.Generic[K, V], object):
          def foo(self, x: K) -> K: ...
          def bar(self, x: V) -> V: ...
          def baz(self, x: Union[K, V]) -> Union[K, V]: ...
          def lorem(self, x: Union[K, V]) -> Union[K, V]: ...
          def ipsum(self, x: K) -> K: ...
      """)
    tree = self.Parse(src)
    new_tree = tree.Visit(optimize.MergeTypeParameters())
    self.AssertSourceEquals(new_tree, expected)

  def test_overloads_not_flattened(self):
    # This test checks that @overloaded functions are not flattened into a
    # single signature.
    src = pytd_src("""
      from typing import overload
      @overload
      def f(x: int) -> str: ...
      @overload
      def f(x: str) -> str: ...
    """)
    self.AssertOptimizeEquals(src, src)


if __name__ == "__main__":
  unittest.main()
