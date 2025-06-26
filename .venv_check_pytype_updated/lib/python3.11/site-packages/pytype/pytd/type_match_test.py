"""Tests for type_match.py."""

import textwrap

from pytype.pyi import parser
from pytype.pytd import booleq
from pytype.pytd import escape
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import type_match
from pytype.pytd import visitors
from pytype.pytd.parse import parser_test_base

import unittest


_BUILTINS = """
  class object: ...
  class classobj: ...
"""


def pytd_src(text):
  text = textwrap.dedent(escape.preprocess_pytd(text))
  text = text.replace("`", "")
  return text


class TestTypeMatch(parser_test_base.ParserTest):
  """Test algorithms and datastructures of booleq.py."""

  def setUp(self):
    super().setUp()
    builtins = parser.parse_string(
        textwrap.dedent(_BUILTINS), name="builtins", options=self.options
    )
    typing = parser.parse_string(
        "class Generic: ...", name="typing", options=self.options
    )
    self.mini_builtins = pytd_utils.Concat(builtins, typing)

  def LinkAgainstSimpleBuiltins(self, ast):
    ast = ast.Visit(visitors.AdjustTypeParameters())
    ast = visitors.LookupClasses(ast, self.mini_builtins)
    return ast

  def assertMatch(self, m, t1, t2):
    eq = m.match_type_against_type(t1, t2, {})
    self.assertEqual(eq, booleq.TRUE)

  def assertNoMatch(self, m, t1, t2):
    eq = m.match_type_against_type(t1, t2, {})
    self.assertEqual(eq, booleq.FALSE)

  def test_anything(self):
    m = type_match.TypeMatch({})
    self.assertMatch(m, pytd.AnythingType(), pytd.AnythingType())
    self.assertMatch(m, pytd.AnythingType(), pytd.NamedType("x"))
    self.assertMatch(m, pytd.NamedType("x"), pytd.AnythingType())

  def test_anything_as_top(self):
    m = type_match.TypeMatch({}, any_also_is_bottom=False)
    self.assertMatch(m, pytd.AnythingType(), pytd.AnythingType())
    self.assertNoMatch(m, pytd.AnythingType(), pytd.NamedType("x"))
    self.assertMatch(m, pytd.NamedType("x"), pytd.AnythingType())

  def test_nothing_left(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.NothingType(), pytd.NamedType("A"), {})
    self.assertEqual(eq, booleq.TRUE)

  def test_nothing_right(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.NamedType("A"), pytd.NothingType(), {})
    self.assertEqual(eq, booleq.FALSE)

  def test_nothing_nothing(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.NothingType(), pytd.NothingType(), {})
    self.assertEqual(eq, booleq.TRUE)

  def test_nothing_anything(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.NothingType(), pytd.AnythingType(), {})
    self.assertEqual(eq, booleq.TRUE)

  def test_anything_nothing(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.AnythingType(), pytd.NothingType(), {})
    self.assertEqual(eq, booleq.TRUE)

  def test_anything_late(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.AnythingType(), pytd.LateType("X"), {})
    self.assertEqual(eq, booleq.TRUE)

  def test_late_anything(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.LateType("X"), pytd.AnythingType(), {})
    self.assertEqual(eq, booleq.TRUE)

  def test_late_named(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.NamedType("X"), pytd.LateType("X"), {})
    self.assertEqual(eq, booleq.FALSE)

  def test_named_late(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.LateType("X"), pytd.NamedType("X"), {})
    self.assertEqual(eq, booleq.FALSE)

  def test_named(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(pytd.NamedType("A"), pytd.NamedType("A"), {})
    self.assertEqual(eq, booleq.TRUE)
    eq = m.match_type_against_type(pytd.NamedType("A"), pytd.NamedType("B"), {})
    self.assertNotEqual(eq, booleq.TRUE)

  def test_named_against_generic(self):
    m = type_match.TypeMatch({})
    eq = m.match_type_against_type(
        pytd.GenericType(pytd.NamedType("A"), ()), pytd.NamedType("A"), {}
    )
    self.assertEqual(eq, booleq.TRUE)

  def test_function(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      def left(a: int) -> int: ...
      def right(a: int) -> int: ...
    """),
        options=self.options,
    )
    m = type_match.TypeMatch()
    self.assertEqual(
        m.match(ast.Lookup("left"), ast.Lookup("right"), {}), booleq.TRUE
    )

  def test_return(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      def left(a: int) -> float: ...
      def right(a: int) -> int: ...
    """),
        options=self.options,
    )
    m = type_match.TypeMatch()
    self.assertNotEqual(
        m.match(ast.Lookup("left"), ast.Lookup("right"), {}), booleq.TRUE
    )

  def test_optional(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      def left(a: int) -> int: ...
      def right(a: int, *args) -> int: ...
    """),
        options=self.options,
    )
    m = type_match.TypeMatch()
    self.assertEqual(
        m.match(ast.Lookup("left"), ast.Lookup("right"), {}), booleq.TRUE
    )

  def test_generic(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      from typing import Any
      T = TypeVar('T')
      class A(typing.Generic[T], object):
        pass
      left = ...  # type: A[Any]
      right = ...  # type: A[Any]
    """),
        options=self.options,
    )
    ast = self.LinkAgainstSimpleBuiltins(ast)
    m = type_match.TypeMatch()
    self.assertEqual(
        m.match_type_against_type(
            ast.Lookup("left").type, ast.Lookup("right").type, {}
        ),
        booleq.TRUE,
    )

  def test_class_match(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      from typing import Any
      class Left():
        def method(self) -> Any: ...
      class Right():
        def method(self) -> Any: ...
        def method2(self) -> Any: ...
    """),
        options=self.options,
    )
    ast = visitors.LookupClasses(ast, self.mini_builtins)
    m = type_match.TypeMatch()
    left, right = ast.Lookup("Left"), ast.Lookup("Right")
    self.assertEqual(m.match(left, right, {}), booleq.TRUE)
    self.assertNotEqual(m.match(right, left, {}), booleq.TRUE)

  def test_subclasses(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      class A():
        pass
      class B(A):
        pass
      a = ...  # type: A
      def left(a: B) -> B: ...
      def right(a: A) -> A: ...
    """),
        options=self.options,
    )
    ast = visitors.LookupClasses(ast, self.mini_builtins)
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    left, right = ast.Lookup("left"), ast.Lookup("right")
    self.assertEqual(m.match(left, right, {}), booleq.TRUE)
    self.assertNotEqual(m.match(right, left, {}), booleq.TRUE)

  def _TestTypeParameters(self, reverse=False):
    ast = parser.parse_string(
        pytd_src("""
      from typing import Any, Generic
      class `~unknown0`():
        def next(self) -> Any: ...
      T = TypeVar('T')
      class A(Generic[T], object):
        def next(self) -> Any: ...
      class B():
        pass
      def left(x: `~unknown0`) -> Any: ...
      def right(x: A[B]) -> Any: ...
    """),
        options=self.options,
    )
    ast = self.LinkAgainstSimpleBuiltins(ast)
    m = type_match.TypeMatch()
    left, right = ast.Lookup("left"), ast.Lookup("right")
    match = m.match(right, left, {}) if reverse else m.match(left, right, {})
    unknown0 = escape.unknown(0)
    self.assertEqual(
        match,
        booleq.And(
            (booleq.Eq(unknown0, "A"), booleq.Eq(f"{unknown0}.A.T", "B"))
        ),
    )
    self.assertIn(f"{unknown0}.A.T", m.solver.variables)

  def test_unknown_against_generic(self):
    self._TestTypeParameters()

  def test_generic_against_unknown(self):
    self._TestTypeParameters(reverse=True)

  def test_strict(self):
    ast = parser.parse_string(
        pytd_src("""
      import typing

      T = TypeVar('T')
      class list(typing.Generic[T], object):
        pass
      class A():
        pass
      class B(A):
        pass
      class `~unknown0`():
        pass
      a = ...  # type: A
      def left() -> `~unknown0`: ...
      def right() -> list[A]: ...
    """),
        options=self.options,
    )
    ast = self.LinkAgainstSimpleBuiltins(ast)
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    left, right = ast.Lookup("left"), ast.Lookup("right")
    unknown0 = escape.unknown(0)
    self.assertEqual(
        m.match(left, right, {}),
        booleq.And(
            (booleq.Eq(unknown0, "list"), booleq.Eq(f"{unknown0}.list.T", "A"))
        ),
    )

  def test_base_class(self):
    ast = parser.parse_string(
        textwrap.dedent("""
      class Base():
        def f(self, x:Base) -> Base: ...
      class Foo(Base):
        pass

      class Match():
        def f(self, x:Base) -> Base: ...
    """),
        options=self.options,
    )
    ast = self.LinkAgainstSimpleBuiltins(ast)
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    eq = m.match_Class_against_Class(ast.Lookup("Match"), ast.Lookup("Foo"), {})
    self.assertEqual(eq, booleq.TRUE)

  def test_homogeneous_tuple(self):
    ast = self.ParseWithBuiltins("""
      from typing import Tuple
      x1 = ...  # type: Tuple[bool, ...]
      x2 = ...  # type: Tuple[int, ...]
    """)
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    x1 = ast.Lookup("x1").type
    x2 = ast.Lookup("x2").type
    self.assertEqual(m.match_Generic_against_Generic(x1, x1, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(x1, x2, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(x2, x1, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(x2, x2, {}), booleq.TRUE)

  def test_heterogeneous_tuple(self):
    ast = self.ParseWithBuiltins("""
      from typing import Tuple
      x1 = ...  # type: Tuple[int]
      x2 = ...  # type: Tuple[bool, str]
      x3 = ...  # type: Tuple[int, str]
    """)
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    x1 = ast.Lookup("x1").type
    x2 = ast.Lookup("x2").type
    x3 = ast.Lookup("x3").type
    self.assertEqual(m.match_Generic_against_Generic(x1, x1, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(x1, x2, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(x1, x3, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(x2, x1, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(x2, x2, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(x2, x3, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(x3, x1, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(x3, x2, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(x3, x3, {}), booleq.TRUE)

  def test_tuple(self):
    ast = self.ParseWithBuiltins("""
      from typing import Tuple
      x1 = ...  # type: Tuple[bool, ...]
      x2 = ...  # type: Tuple[int, ...]
      y1 = ...  # type: Tuple[bool, int]
    """)
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    x1 = ast.Lookup("x1").type
    x2 = ast.Lookup("x2").type
    y1 = ast.Lookup("y1").type
    # Tuple[T, ...] matches Tuple[U, V] when T matches both U and V.
    self.assertEqual(m.match_Generic_against_Generic(x1, y1, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(x2, y1, {}), booleq.FALSE)
    # Tuple[U, V] matches Tuple[T, ...] when Union[U, V] matches T.
    self.assertEqual(m.match_Generic_against_Generic(y1, x1, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(y1, x2, {}), booleq.TRUE)

  def test_unknown_against_tuple(self):
    ast = self.ParseWithBuiltins(pytd_src("""
      from typing import Tuple
      class `~unknown0`():
        pass
      x = ...  # type: Tuple[int, str]
    """))
    unknown0 = escape.unknown(0)
    unk = ast.Lookup(unknown0)
    tup = ast.Lookup("x").type
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    match = m.match_Unknown_against_Generic(unk, tup, {})
    self.assertCountEqual(
        sorted(match.extract_equalities()),
        [
            (unknown0, "builtins.tuple"),
            (f"{unknown0}.builtins.tuple._T", "int"),
            (f"{unknown0}.builtins.tuple._T", "str"),
        ],
    )

  def test_function_against_tuple_subclass(self):
    ast = self.ParseWithBuiltins("""
      from typing import Tuple
      class A(Tuple[int, str]): ...
      def f(x): ...
    """)
    a = ast.Lookup("A")
    f = ast.Lookup("f")
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    # Smoke test for the TupleType logic in match_Function_against_Class
    self.assertEqual(m.match_Function_against_Class(f, a, {}, {}), booleq.FALSE)

  def test_callable_no_arguments(self):
    ast = self.ParseWithBuiltins("""
      from typing import Callable
      v1 = ...  # type: Callable[..., int]
      v2 = ...  # type: Callable[..., bool]
    """)
    v1 = ast.Lookup("v1").type
    v2 = ast.Lookup("v2").type
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    # Return type is covariant.
    self.assertEqual(m.match_Generic_against_Generic(v1, v2, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v2, v1, {}), booleq.TRUE)

  def test_callable_with_arguments(self):
    ast = self.ParseWithBuiltins("""
      from typing import Callable
      v1 = ...  # type: Callable[[int], int]
      v2 = ...  # type: Callable[[bool], int]
      v3 = ...  # type: Callable[[int], bool]
      v4 = ...  # type: Callable[[int, str], int]
      v5 = ...  # type: Callable[[bool, str], int]
      v6 = ...  # type: Callable[[], int]
    """)
    v1 = ast.Lookup("v1").type
    v2 = ast.Lookup("v2").type
    v3 = ast.Lookup("v3").type
    v4 = ast.Lookup("v4").type
    v5 = ast.Lookup("v5").type
    v6 = ast.Lookup("v6").type
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    # Argument types are contravariant.
    self.assertEqual(m.match_Generic_against_Generic(v1, v2, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v2, v1, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v1, v4, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v4, v1, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v4, v5, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v5, v4, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v1, v6, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v6, v1, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v6, v6, {}), booleq.TRUE)
    # Return type is covariant.
    self.assertEqual(m.match_Generic_against_Generic(v1, v3, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v3, v1, {}), booleq.TRUE)

  def test_callable(self):
    ast = self.ParseWithBuiltins("""
      from typing import Callable
      v1 = ...  # type: Callable[..., int]
      v2 = ...  # type: Callable[..., bool]
      v3 = ...  # type: Callable[[int, str], int]
      v4 = ...  # type: Callable[[int, str], bool]
      v5 = ...  # type: Callable[[], int]
      v6 = ...  # type: Callable[[], bool]
    """)
    v1 = ast.Lookup("v1").type
    v2 = ast.Lookup("v2").type
    v3 = ast.Lookup("v3").type
    v4 = ast.Lookup("v4").type
    v5 = ast.Lookup("v5").type
    v6 = ast.Lookup("v6").type
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    self.assertEqual(m.match_Generic_against_Generic(v1, v4, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v4, v1, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v2, v3, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v3, v2, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v2, v5, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v5, v2, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v1, v6, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v6, v1, {}), booleq.TRUE)

  def test_callable_and_type(self):
    ast = self.ParseWithBuiltins("""
      from typing import Callable, Type
      v1 = ...  # type: Callable[..., int]
      v2 = ...  # type: Callable[..., bool]
      v3 = ...  # type: Callable[[], int]
      v4 = ...  # type: Callable[[], bool]
      v5 = ...  # type: Type[int]
      v6 = ...  # type: Type[bool]
    """)
    v1 = ast.Lookup("v1").type
    v2 = ast.Lookup("v2").type
    v3 = ast.Lookup("v3").type
    v4 = ast.Lookup("v4").type
    v5 = ast.Lookup("v5").type
    v6 = ast.Lookup("v6").type
    m = type_match.TypeMatch(type_match.get_all_subclasses([ast]))
    self.assertEqual(m.match_Generic_against_Generic(v1, v6, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v6, v1, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v2, v5, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v5, v2, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v3, v6, {}), booleq.FALSE)
    self.assertEqual(m.match_Generic_against_Generic(v6, v3, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v4, v5, {}), booleq.TRUE)
    self.assertEqual(m.match_Generic_against_Generic(v5, v4, {}), booleq.FALSE)


if __name__ == "__main__":
  unittest.main()
