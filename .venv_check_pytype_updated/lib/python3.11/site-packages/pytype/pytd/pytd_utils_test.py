import textwrap

from pytype.pyi import parser
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.pytd.parse import parser_test_base

import unittest


class TestUtils(parser_test_base.ParserTest):
  """Test pytype.pytd.pytd_utils."""

  def test_unpack_union(self):
    """Test for UnpackUnion."""
    ast = self.Parse("""
      from typing import Union
      c1 = ...  # type: Union[int, float]
      c2 = ...  # type: int
      c3 = ...  # type: list[Union[int, float]]""")
    c1 = ast.Lookup("c1").type
    c2 = ast.Lookup("c2").type
    c3 = ast.Lookup("c3").type
    self.assertCountEqual(pytd_utils.UnpackUnion(c1), c1.type_list)
    self.assertCountEqual(pytd_utils.UnpackUnion(c2), [c2])
    self.assertCountEqual(pytd_utils.UnpackUnion(c3), [c3])

  def test_concat(self):
    """Test for concatenating two pytd ASTs."""
    ast1 = self.Parse("""
      c1 = ...  # type: int

      def f1() -> int: ...

      class Class1:
        pass
    """)
    ast2 = self.Parse("""
      c2 = ...  # type: int

      def f2() -> int: ...

      class Class2:
        pass
    """)
    expected = textwrap.dedent("""
      c1 = ...  # type: int
      c2 = ...  # type: int

      def f1() -> int: ...
      def f2() -> int: ...

      class Class1:
          pass

      class Class2:
          pass
    """)
    combined = pytd_utils.Concat(ast1, ast2)
    self.AssertSourceEquals(combined, expected)

  def test_concat3(self):
    """Test for concatenating three pytd ASTs."""
    ast1 = self.Parse("""c1 = ...  # type: int""")
    ast2 = self.Parse("""c2 = ...  # type: float""")
    ast3 = self.Parse("""c3 = ...  # type: bool""")
    combined = pytd_utils.Concat(ast1, ast2, ast3)
    expected = textwrap.dedent("""
      c1 = ...  # type: int
      c2 = ...  # type: float
      c3 = ...  # type: bool
    """)
    self.AssertSourceEquals(combined, expected)

  def test_concat_type_parameters(self):
    """Test for concatenating ASTs with type parameters."""
    ast1 = self.Parse("""T = TypeVar("T")""", name="builtins")
    ast2 = self.Parse("""T = TypeVar("T")""")
    combined = pytd_utils.Concat(ast1, ast2)
    self.assertEqual(
        combined.Lookup("builtins.T"), pytd.TypeParameter("T", scope="builtins")
    )
    self.assertEqual(combined.Lookup("T"), pytd.TypeParameter("T", scope=None))

  def test_join_types(self):
    """Test that JoinTypes() does recursive flattening."""
    n1, n2, n3, n4, n5, n6 = (pytd.NamedType("n%d" % i) for i in range(6))
    # n1 or (n2 or (n3))
    nested1 = pytd.UnionType((n1, pytd.UnionType((n2, pytd.UnionType((n3,))))))
    # ((n4) or n5) or n6
    nested2 = pytd.UnionType((pytd.UnionType((pytd.UnionType((n4,)), n5)), n6))
    joined = pytd_utils.JoinTypes([nested1, nested2])
    self.assertEqual(joined.type_list, (n1, n2, n3, n4, n5, n6))

  def test_join_single_type(self):
    """Test that JoinTypes() returns single types as-is."""
    a = pytd.NamedType("a")
    self.assertEqual(pytd_utils.JoinTypes([a]), a)
    self.assertEqual(pytd_utils.JoinTypes([a, a]), a)

  def test_join_nothing_type(self):
    """Test that JoinTypes() removes or collapses 'nothing'."""
    a = pytd.NamedType("a")
    nothing = pytd.NothingType()
    self.assertEqual(pytd_utils.JoinTypes([a, nothing]), a)
    self.assertEqual(pytd_utils.JoinTypes([nothing]), nothing)
    self.assertEqual(pytd_utils.JoinTypes([nothing, nothing]), nothing)

  def test_join_empty_types_to_nothing(self):
    """Test that JoinTypes() simplifies empty unions to 'nothing'."""
    self.assertIsInstance(pytd_utils.JoinTypes([]), pytd.NothingType)

  def test_join_anything_types(self):
    """Test that JoinTypes() simplifies unions containing 'Any'."""
    types = [pytd.AnythingType(), pytd.NamedType("a")]
    self.assertIsInstance(pytd_utils.JoinTypes(types), pytd.AnythingType)

  def test_join_optional_anything_types(self):
    """Test that JoinTypes() simplifies unions containing 'Any' and 'None'."""
    any_type = pytd.AnythingType()
    none_type = pytd.NamedType("builtins.NoneType")
    types = [pytd.NamedType("a"), any_type, none_type]
    self.assertEqual(
        pytd_utils.JoinTypes(types), pytd.UnionType((any_type, none_type))
    )

  def test_type_matcher(self):
    """Test for the TypeMatcher class."""

    class MyTypeMatcher(pytd_utils.TypeMatcher):

      def default_match(self, t1, t2, mykeyword):
        assert mykeyword == "foobar"
        return t1 == t2

      def match_Function_against_Function(self, f1, f2, mykeyword):
        assert mykeyword == "foobar"
        return all(
            self.match(sig1, sig2, mykeyword)
            for sig1, sig2 in zip(f1.signatures, f2.signatures)
        )

    s1 = pytd.Signature((), None, None, pytd.NothingType(), (), ())
    s2 = pytd.Signature((), None, None, pytd.AnythingType(), (), ())
    self.assertTrue(
        MyTypeMatcher().match(
            pytd.Function("f1", (s1, s2), pytd.MethodKind.METHOD),
            pytd.Function("f2", (s1, s2), pytd.MethodKind.METHOD),
            mykeyword="foobar",
        )
    )
    self.assertFalse(
        MyTypeMatcher().match(
            pytd.Function("f1", (s1, s2), pytd.MethodKind.METHOD),
            pytd.Function("f2", (s2, s2), pytd.MethodKind.METHOD),
            mykeyword="foobar",
        )
    )

  def test_named_type_with_module(self):
    """Test NamedTypeWithModule()."""
    self.assertEqual(
        pytd_utils.NamedTypeWithModule("name"), pytd.NamedType("name")
    )
    self.assertEqual(
        pytd_utils.NamedTypeWithModule("name", None), pytd.NamedType("name")
    )
    self.assertEqual(
        pytd_utils.NamedTypeWithModule("name", "package"),
        pytd.NamedType("package.name"),
    )

  def test_ordered_set(self):
    ordered_set = pytd_utils.OrderedSet(n // 2 for n in range(10))
    ordered_set.add(-42)
    ordered_set.add(3)
    self.assertEqual(tuple(ordered_set), (0, 1, 2, 3, 4, -42))

  def test_wrap_type_decl_unit(self):
    """Test WrapTypeDeclUnit."""
    ast1 = self.Parse("""
      c = ...  # type: int
      def f(x: int) -> int: ...
      def f(x: float) -> float: ...
      class A:
        pass
    """)
    ast2 = self.Parse("""
      c = ...  # type: float
      d = ...  # type: int
      def f(x: complex) -> complex: ...
      class B:
        pass
    """)
    w = pytd_utils.WrapTypeDeclUnit(
        "combined",
        ast1.classes
        + ast1.functions
        + ast1.constants
        + ast2.classes
        + ast2.functions
        + ast2.constants,
    )
    expected = textwrap.dedent("""
      from typing import Union
      c = ...  # type: Union[int, float]
      d = ...  # type: int
      def f(x: int) -> int: ...
      def f(x: float) -> float: ...
      def f(x: complex) -> complex: ...
      class A:
        pass
      class B:
        pass
    """)
    self.AssertSourceEquals(w, expected)

  def test_builtin_alias(self):
    src = "Number = int"
    ast = parser.parse_string(src, options=self.options)
    self.assertMultiLineEqual(pytd_utils.Print(ast), src)

  def test_typing_name_conflict1(self):
    src = textwrap.dedent("""
      import typing

      x: typing.List[str]

      def List() -> None: ...
    """)
    ast = parser.parse_string(src, options=self.options)
    expected = textwrap.dedent("""
      import typing

      x: list[str]

      def List() -> None: ...
    """)
    self.assertMultiLineEqual(
        pytd_utils.Print(ast).strip("\n"), expected.strip("\n")
    )

  def test_typing_name_conflict2(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      import typing
      from typing import Any

      x = ...  # type: typing.List[str]

      class MyClass:
          List = ...  # type: Any
          x = ...  # type: typing.List[str]
    """),
        options=self.options,
    )
    expected = textwrap.dedent("""
      import typing
      from typing import Any

      x: list[str]

      class MyClass:
          List: Any
          x: list[str]
    """)
    self.assertMultiLineEqual(
        pytd_utils.Print(ast).strip("\n"), expected.strip("\n")
    )

  def test_dummy_method(self):
    self.assertEqual(
        "def foo() -> Any: ...", pytd_utils.Print(pytd_utils.DummyMethod("foo"))
    )
    self.assertEqual(
        "def foo(x) -> Any: ...",
        pytd_utils.Print(pytd_utils.DummyMethod("foo", "x")),
    )
    self.assertEqual(
        "def foo(x, y) -> Any: ...",
        pytd_utils.Print(pytd_utils.DummyMethod("foo", "x", "y")),
    )

  def test_asteq(self):
    # This creates two ASts that are equivalent but whose sources are slightly
    # different. The union types are different (int,str) vs (str,int) but the
    # ordering is ignored when testing for equality (which ASTeq uses).
    src1 = textwrap.dedent("""
        from typing import Union
        def foo(a: Union[int, str]) -> C: ...
        T = TypeVar('T')
        class C(typing.Generic[T], object):
            def bar(x: T) -> NoneType: ...
        CONSTANT = ...  # type: C[float]
        """)
    src2 = textwrap.dedent("""
        from typing import Union
        CONSTANT = ...  # type: C[float]
        T = TypeVar('T')
        class C(typing.Generic[T], object):
            def bar(x: T) -> NoneType: ...
        def foo(a: Union[str, int]) -> C: ...
        """)
    tree1 = parser.parse_string(src1, options=self.options)
    tree2 = parser.parse_string(src2, options=self.options)
    tree1.Visit(visitors.VerifyVisitor())
    tree2.Visit(visitors.VerifyVisitor())
    self.assertTrue(tree1.constants)
    self.assertTrue(tree1.classes)
    self.assertTrue(tree1.functions)
    self.assertTrue(tree2.constants)
    self.assertTrue(tree2.classes)
    self.assertTrue(tree2.functions)
    self.assertIsInstance(tree1, pytd.TypeDeclUnit)
    self.assertIsInstance(tree2, pytd.TypeDeclUnit)
    # For the ==, != tests, TypeDeclUnit uses identity
    # pylint: disable=g-generic-assert
    # pylint: disable=comparison-with-itself
    self.assertTrue(tree1 == tree1)
    self.assertTrue(tree2 == tree2)
    self.assertFalse(tree1 == tree2)
    self.assertFalse(tree2 == tree1)
    self.assertFalse(tree1 != tree1)
    self.assertFalse(tree2 != tree2)
    self.assertTrue(tree1 != tree2)
    self.assertTrue(tree2 != tree1)
    # pylint: enable=g-generic-assert
    # pylint: enable=comparison-with-itself
    self.assertEqual(tree1, tree1)
    self.assertEqual(tree2, tree2)
    self.assertNotEqual(tree1, tree2)
    self.assertTrue(pytd_utils.ASTeq(tree1, tree2))
    self.assertTrue(pytd_utils.ASTeq(tree1, tree1))
    self.assertTrue(pytd_utils.ASTeq(tree2, tree1))
    self.assertTrue(pytd_utils.ASTeq(tree2, tree2))

  def test_type_builder(self):
    t = pytd_utils.TypeBuilder()
    self.assertFalse(t)
    t.add_type(pytd.AnythingType())
    self.assertTrue(t)


class PrintTest(parser_test_base.ParserTest):
  """Test pytd_utils.Print."""

  def test_smoke(self):
    """Smoketest for printing pytd."""
    ast = self.Parse("""
      from typing import Any, Union
      c1 = ...  # type: int
      T = TypeVar('T')
      class A(typing.Generic[T], object):
        bar = ...  # type: T
        def foo(self, x: list[int], y: T) -> Union[list[T], float]:
          raise ValueError()
      X = TypeVar('X')
      Y = TypeVar('Y')
      def bar(x: Union[X, Y]) -> Any: ...
    """)
    pytd_utils.Print(ast)

  def test_literal(self):
    ast = self.Parse("""
      from typing import Literal
      x1: Literal[""]
      x2: Literal[b""]
      x3: Literal[0]
      x4: Literal[True]
      x5: Literal[None]
    """)
    ast = ast.Visit(visitors.LookupBuiltins(self.loader.builtins))
    self.assertMultiLineEqual(
        pytd_utils.Print(ast),
        textwrap.dedent("""
      from typing import Literal

      x1: Literal['']
      x2: Literal[b'']
      x3: Literal[0]
      x4: Literal[True]
      x5: None
    """).strip(),
    )

  def test_literal_union(self):
    ast = self.Parse("""
      from typing import Literal, Union
      x: Union[Literal["x"], Literal["y"]]
    """)
    self.assertMultiLineEqual(
        pytd_utils.Print(ast),
        textwrap.dedent("""
      from typing import Literal

      x: Literal['x', 'y']
    """).strip(),
    )

  def test_reuse_union_name(self):
    src = """
      import typing
      from typing import Callable, Iterable

      class Node: ...

      class Union:
          _predicates: tuple[Callable[[typing.Union[Iterable[Node], Node]], bool], ...]
          def __init__(self, *predicates: Callable[[typing.Union[Iterable[Node], Node]], bool]) -> None: ...
    """
    ast = self.Parse(src)
    self.assertMultiLineEqual(
        pytd_utils.Print(ast), textwrap.dedent(src).strip()
    )


if __name__ == "__main__":
  unittest.main()
