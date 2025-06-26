"""Tests for matcher.py."""

import textwrap

from pytype import config
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.errors import error_types
from pytype.tests import test_base
from pytype.tests import test_utils
from pytype.types import types

import unittest


class MatcherTestBase(test_base.UnitTest):

  def setUp(self):
    super().setUp()
    options = config.Options.create(
        python_version=self.python_version, none_is_not_bool=True
    )
    self.ctx = test_utils.make_context(options)
    self.matcher = self.ctx.matcher(self.ctx.root_node)

  def _match_var(self, left, right):
    var = self.ctx.program.NewVariable()
    var.AddBinding(left, [], self.ctx.root_node)
    for view in abstract_utils.get_views([var], self.ctx.root_node):
      yield self.matcher.match_var_against_type(var, right, {}, view)

  def assertMatch(self, left, right):
    for match in self._match_var(left, right):
      self.assertEqual(match, {})

  def assertNoMatch(self, left, right):
    for match in self._match_var(left, right):
      self.assertIsNone(match)


class MatcherTest(MatcherTestBase):
  """Test matcher.AbstractMatcher."""

  def _make_class(self, name):
    return abstract.InterpreterClass(name, [], {}, None, None, (), self.ctx)

  def _parse_and_lookup(self, src, objname, filename=None):
    if filename is None:
      filename = str(hash(src))
    with test_utils.Tempdir() as d:
      d.create_file(filename + ".pyi", src)
      self.ctx.options.tweak(pythonpath=[d.path])  # monkeypatch
      return self.ctx.loader.lookup_pytd(filename, objname)

  def _convert(self, x, name, as_instance=False):
    pyval = self._parse_and_lookup(x, name)
    if as_instance:
      pyval = abstract_utils.AsInstance(pyval)
    return self.ctx.convert.constant_to_value(pyval, {}, self.ctx.root_node)

  def _convert_type(self, t, as_instance=False):
    """Convenience function for turning a string into an abstract value.

    Note that this function cannot be called more than once per test with
    the same arguments, since we hash the arguments to get a filename for
    the temporary pyi.

    Args:
      t: The string representation of a type.
      as_instance: Whether to convert as an instance.

    Returns:
      A BaseValue.
    """
    src = textwrap.dedent(f"""
      from typing import Any, Callable, Iterator, Tuple, Type, Union
      from protocols import Sequence, SupportsLower
      x = ...  # type: {t}
    """)
    filename = str(hash((t, as_instance)))
    x = self._parse_and_lookup(src, "x", filename).type
    if as_instance:
      x = abstract_utils.AsInstance(x)
    return self.ctx.convert.constant_to_value(x, {}, self.ctx.root_node)

  def test_basic(self):
    self.assertMatch(abstract.Empty(self.ctx), abstract.Empty(self.ctx))

  def test_type(self):
    left = self._make_class("dummy")
    type_parameters = {
        abstract_utils.T: abstract.TypeParameter(abstract_utils.T, self.ctx)
    }
    other_type = abstract.ParameterizedClass(
        self.ctx.convert.type_type, type_parameters, self.ctx
    )
    for result in self._match_var(left, other_type):
      (instance_binding,) = result[abstract_utils.T].bindings
      self.assertEqual(instance_binding.data.cls, left)

  def test_union(self):
    left_option1 = self._make_class("o1")
    left_option2 = self._make_class("o2")
    left = abstract.Union([left_option1, left_option2], self.ctx)
    self.assertMatch(left, self.ctx.convert.type_type)

  def test_metaclass(self):
    left = self._make_class("left")
    meta1 = self._make_class("m1")
    meta2 = self._make_class("m2")
    left.set_class(
        self.ctx.root_node,
        self.ctx.program.NewVariable([meta1, meta2], [], self.ctx.root_node),
    )
    self.assertMatch(left, meta1)
    self.assertMatch(left, meta2)

  def test_empty_against_class(self):
    var = self.ctx.program.NewVariable()
    right = self._make_class("bar")
    result = self.matcher.match_var_against_type(var, right, {}, {})
    self.assertEqual(result, {})

  def test_empty_var_against_empty(self):
    var = self.ctx.program.NewVariable()
    right = abstract.Empty(self.ctx)
    result = self.matcher.match_var_against_type(var, right, {}, {})
    self.assertEqual(result, {})

  def test_empty_against_type_parameter(self):
    var = self.ctx.program.NewVariable()
    right = abstract.TypeParameter("T", self.ctx)
    result = self.matcher.match_var_against_type(var, right, {}, {})
    self.assertCountEqual(result.keys(), ["T"])
    self.assertFalse(result["T"].bindings)

  def test_empty_against_unsolvable(self):
    var = self.ctx.program.NewVariable()
    right = abstract.Empty(self.ctx)
    result = self.matcher.match_var_against_type(var, right, {}, {})
    self.assertEqual(result, {})

  def test_class_against_type_union(self):
    left = self._make_class("foo")
    union = abstract.Union((left,), self.ctx)
    right = abstract.ParameterizedClass(
        self.ctx.convert.type_type, {abstract_utils.T: union}, self.ctx
    )
    self.assertMatch(left, right)

  def test_none_against_bool(self):
    left = self._convert_type("None", as_instance=True)
    right = self._convert_type("bool")
    self.assertNoMatch(left, right)

  def test_homogeneous_tuple(self):
    left = self._convert_type("Tuple[int, ...]", as_instance=True)
    right1 = self._convert_type("Tuple[int, ...]")
    right2 = self._convert_type("Tuple[str, ...]")
    self.assertMatch(left, right1)
    self.assertNoMatch(left, right2)

  def test_heterogeneous_tuple(self):
    left1 = self._convert_type("Tuple[Union[int, str]]", as_instance=True)
    left2 = self._convert_type("Tuple[int, str]", as_instance=True)
    left3 = self._convert_type("Tuple[str, int]", as_instance=True)
    right = self._convert_type("Tuple[int, str]")
    self.assertNoMatch(left1, right)
    self.assertMatch(left2, right)
    self.assertNoMatch(left3, right)

  def test_heterogeneous_tuple_against_homogeneous_tuple(self):
    left = self._convert_type("Tuple[bool, int]", as_instance=True)
    right1 = self._convert_type("Tuple[bool, ...]")
    right2 = self._convert_type("Tuple[int, ...]")
    right3 = self._convert_type("tuple")
    self.assertNoMatch(left, right1)
    self.assertMatch(left, right2)
    self.assertMatch(left, right3)

  def test_homogeneous_tuple_against_heterogeneous_tuple(self):
    left1 = self._convert_type("Tuple[bool, ...]", as_instance=True)
    left2 = self._convert_type("Tuple[int, ...]", as_instance=True)
    left3 = self._convert_type("tuple", as_instance=True)
    right = self._convert_type("Tuple[bool, int]")
    self.assertMatch(left1, right)
    self.assertNoMatch(left2, right)
    self.assertMatch(left3, right)

  def test_tuple_type(self):
    # homogeneous against homogeneous
    left = self._convert_type("Type[Tuple[float, ...]]", as_instance=True)
    right1 = self._convert_type("Type[Tuple[float, ...]]")
    right2 = self._convert_type("Type[Tuple[str, ...]]")
    self.assertMatch(left, right1)
    self.assertNoMatch(left, right2)

    # heterogeneous against heterogeneous
    left1 = self._convert_type("Type[Tuple[Union[int, str]]]", as_instance=True)
    left2 = self._convert_type("Type[Tuple[int, str]]", as_instance=True)
    left3 = self._convert_type("Type[Tuple[str, int]]", as_instance=True)
    right = self._convert_type("Type[Tuple[int, str]]")
    self.assertNoMatch(left1, right)
    self.assertMatch(left2, right)
    self.assertNoMatch(left3, right)

    # heterogeneous against homogeneous
    left = self._convert_type("Type[Tuple[bool, int]]", as_instance=True)
    right1 = self._convert_type("Type[Tuple[bool, ...]]")
    right2 = self._convert_type("Type[Tuple[int, ...]]")
    right3 = self._convert_type("Type[tuple]")
    self.assertNoMatch(left, right1)
    self.assertMatch(left, right2)
    self.assertMatch(left, right3)

    # homogeneous against heterogeneous
    left1 = self._convert_type("Type[Tuple[bool, ...]]", as_instance=True)
    left2 = self._convert_type("Type[Tuple[int, ...]]", as_instance=True)
    left3 = self._convert_type("Type[tuple]", as_instance=True)
    right = self._convert_type("Type[Tuple[bool, int]]")
    self.assertMatch(left1, right)
    self.assertNoMatch(left2, right)
    self.assertMatch(left3, right)

  def test_tuple_subclass(self):
    left = self._convert(
        """
      from typing import Tuple
      class A(Tuple[bool, int]): ...""",
        "A",
        as_instance=True,
    )
    right1 = self._convert_type("Tuple[bool, int]")
    right2 = self._convert_type("Tuple[int, bool]")
    right3 = self._convert_type("Tuple[int, int]")
    right4 = self._convert_type("Tuple[int]")
    right5 = self._convert_type("tuple")
    right6 = self._convert_type("Tuple[bool, ...]")
    right7 = self._convert_type("Tuple[int, ...]")
    self.assertMatch(left, right1)
    self.assertNoMatch(left, right2)
    self.assertMatch(left, right3)
    self.assertNoMatch(left, right4)
    self.assertMatch(left, right5)
    self.assertNoMatch(left, right6)
    self.assertMatch(left, right7)

  def test_annotation_class(self):
    left = abstract.AnnotationClass("Dict", self.ctx)
    right = self.ctx.convert.object_type
    self.assertMatch(left, right)

  def test_empty_tuple_class(self):
    var = self.ctx.program.NewVariable()
    params = {
        0: abstract.TypeParameter(abstract_utils.K, self.ctx),
        1: abstract.TypeParameter(abstract_utils.V, self.ctx),
    }
    params[abstract_utils.T] = abstract.Union((params[0], params[1]), self.ctx)
    right = abstract.TupleClass(self.ctx.convert.tuple_type, params, self.ctx)
    match = self.matcher.match_var_against_type(var, right, {}, {})
    self.assertSetEqual(set(match), {abstract_utils.K, abstract_utils.V})

  def test_unsolvable_against_tuple_class(self):
    left = self.ctx.convert.unsolvable
    params = {
        0: abstract.TypeParameter(abstract_utils.K, self.ctx),
        1: abstract.TypeParameter(abstract_utils.V, self.ctx),
    }
    params[abstract_utils.T] = abstract.Union((params[0], params[1]), self.ctx)
    right = abstract.TupleClass(self.ctx.convert.tuple_type, params, self.ctx)
    for match in self._match_var(left, right):
      self.assertSetEqual(set(match), {abstract_utils.K, abstract_utils.V})
      self.assertEqual(
          match[abstract_utils.K].data, [self.ctx.convert.unsolvable]
      )
      self.assertEqual(
          match[abstract_utils.V].data, [self.ctx.convert.unsolvable]
      )

  def test_bool_against_float(self):
    left = self.ctx.convert.true
    right = self.ctx.convert.primitive_classes[float]
    self.assertMatch(left, right)

  def test_pytd_function_against_callable(self):
    f = self._convert("def f(x: int) -> bool: ...", "f")
    plain_callable = self._convert_type("Callable")
    good_callable1 = self._convert_type("Callable[[bool], int]")
    good_callable2 = self._convert_type("Callable[..., int]")
    self.assertMatch(f, plain_callable)
    self.assertMatch(f, good_callable1)
    self.assertMatch(f, good_callable2)

  def test_pytd_function_against_callable_bad_return(self):
    f = self._convert("def f(x: int) -> bool: ...", "f")
    callable_bad_ret = self._convert_type("Callable[[int], str]")
    self.assertNoMatch(f, callable_bad_ret)

  def test_pytd_function_against_callable_bad_arg_count(self):
    f = self._convert("def f(x: int) -> bool: ...", "f")
    callable_bad_count1 = self._convert_type("Callable[[], bool]")
    callable_bad_count2 = self._convert_type("Callable[[int, str], bool]")
    self.assertNoMatch(f, callable_bad_count1)
    self.assertNoMatch(f, callable_bad_count2)

  def test_pytd_function_against_callable_bad_arg_type(self):
    f = self._convert("def f(x: bool) -> bool: ...", "f")
    callable_bad_arg1 = self._convert_type("Callable[[int], bool]")
    callable_bad_arg2 = self._convert_type("Callable[[str], bool]")
    self.assertNoMatch(f, callable_bad_arg1)
    self.assertNoMatch(f, callable_bad_arg2)

  def test_bound_pytd_function_against_callable(self):
    instance = self._convert(
        """
      class A:
        def f(self, x: int) -> bool: ...
    """,
        "A",
        as_instance=True,
    )
    binding = instance.to_binding(self.ctx.root_node)
    _, var = self.ctx.attribute_handler.get_attribute(
        self.ctx.root_node, instance, "f", binding
    )
    bound = var.data[0]
    _, var = self.ctx.attribute_handler.get_attribute(
        self.ctx.root_node, instance.cls, "f"
    )
    unbound = var.data[0]
    callable_no_self = self._convert_type("Callable[[int], Any]")
    callable_self = self._convert_type("Callable[[Any, int], Any]")
    self.assertMatch(bound, callable_no_self)
    self.assertNoMatch(unbound, callable_no_self)
    self.assertNoMatch(bound, callable_self)
    self.assertMatch(unbound, callable_self)

  def test_native_function_against_callable(self):
    # Matching a native function against a callable always succeeds, regardless
    # of argument and return types.
    f = abstract.NativeFunction("f", lambda x: x, self.ctx)
    callable_type = self._convert_type("Callable[[int], int]")
    self.assertMatch(f, callable_type)

  def test_callable_instance(self):
    left1 = self._convert_type("Callable[[int], bool]", as_instance=True)
    left2 = self._convert_type("Callable", as_instance=True)
    left3 = self._convert_type("Callable[..., int]", as_instance=True)
    right1 = self._convert_type("Callable[[bool], int]")
    right2 = self._convert_type("Callable[..., int]")
    right3 = self._convert_type("Callable")
    self.assertMatch(left1, right1)
    self.assertMatch(left2, right1)
    self.assertMatch(left3, right1)
    self.assertMatch(left1, right2)
    self.assertMatch(left2, right2)
    self.assertMatch(left3, right2)
    self.assertMatch(left1, right3)
    self.assertMatch(left2, right3)
    self.assertMatch(left3, right3)

  def test_callable_instance_bad_return(self):
    left1 = self._convert_type("Callable[[int], float]", as_instance=True)
    left2 = self._convert_type("Callable[..., float]", as_instance=True)
    right1 = self._convert_type("Callable[[bool], int]")
    right2 = self._convert_type("Callable[..., int]")
    self.assertNoMatch(left1, right1)
    self.assertNoMatch(left2, right1)
    self.assertNoMatch(left1, right2)
    self.assertNoMatch(left2, right2)

  def test_callable_instance_bad_arg_count(self):
    left1 = self._convert_type("Callable[[], int]", as_instance=True)
    left2 = self._convert_type("Callable[[str, str], int]", as_instance=True)
    right = self._convert_type("Callable[[str], int]")
    self.assertNoMatch(left1, right)
    self.assertNoMatch(left2, right)

  def test_callable_instance_bad_arg_type(self):
    left1 = self._convert_type("Callable[[bool], Any]", as_instance=True)
    left2 = self._convert_type("Callable[[str], Any]", as_instance=True)
    right = self._convert_type("Callable[[int], Any]")
    self.assertNoMatch(left1, right)
    self.assertNoMatch(left2, right)

  def test_type_against_callable(self):
    left1 = self._convert_type("Type[int]", as_instance=True)
    left2 = self._convert_type("Type[str]", as_instance=True)
    right1 = self._convert_type("Callable[..., float]")
    right2 = self._convert_type("Callable[[], float]")
    self.assertMatch(left1, right1)
    self.assertMatch(left1, right2)
    self.assertNoMatch(left2, right1)
    self.assertNoMatch(left2, right2)

  def test_anystr_instance_against_anystr(self):
    right = self.ctx.convert.lookup_value("typing", "AnyStr")
    dummy_instance = abstract.Instance(self.ctx.convert.tuple_type, self.ctx)
    left = abstract.TypeParameterInstance(right, dummy_instance, self.ctx)
    for result in self._match_var(left, right):
      self.assertCountEqual(
          [(name, var.data) for name, var in result.items()],
          [("typing.AnyStr", [left])],
      )

  def test_protocol(self):
    left1 = self._convert_type("str", as_instance=True)
    left2 = self._convert(
        """
      class A:
        def lower(self) : ...
    """,
        "A",
        as_instance=True,
    )
    left3 = self._convert_type("int", as_instance=True)
    right = self._convert_type("SupportsLower")
    self.assertMatch(left1, right)
    self.assertMatch(left2, right)
    self.assertNoMatch(left3, right)

  def test_protocol_iterator(self):
    left1 = self._convert_type("Iterator", as_instance=True)
    left2 = self._convert(
        """
      class A:
        def __next__(self): ...
        def __iter__(self): ...
    """,
        "A",
        as_instance=True,
    )
    left3 = self._convert_type("int", as_instance=True)
    right = self._convert_type("Iterator")
    self.assertMatch(left1, right)
    self.assertMatch(left2, right)
    self.assertNoMatch(left3, right)

  def test_protocol_sequence(self):
    left1 = self._convert_type("list", as_instance=True)
    left2 = self._convert(
        """
      class A:
        def __getitem__(self, i) : ...
        def __len__(self): ...
    """,
        "A",
        as_instance=True,
    )
    left3 = self._convert_type("int", as_instance=True)
    right = self._convert_type("Sequence")
    self.assertMatch(left1, right)
    self.assertMatch(left2, right)
    self.assertNoMatch(left3, right)

  @unittest.skip("Needs to be fixed, tries to match protocol against A")
  def test_parameterized_protocol(self):
    left1 = self._convert(
        """
      from typing import Iterator
      class A:
        def __iter__(self) -> Iterator[int] : ...
    """,
        "A",
        as_instance=True,
    )
    left2 = self._convert_type("int", as_instance=True)
    right = self._convert_type("Iterable[int]")
    self.assertMatch(left1, right)
    self.assertNoMatch(left2, right)

  def test_never(self):
    self.assertMatch(self.ctx.convert.never, self.ctx.convert.never)

  def test_empty_against_never(self):
    self.assertMatch(self.ctx.convert.empty, self.ctx.convert.never)

  def test_never_against_class(self):
    right = self._convert_type("int")
    self.assertNoMatch(self.ctx.convert.never, right)

  def test_empty_against_parameterized_iterable(self):
    left = self.ctx.convert.empty
    right = abstract.ParameterizedClass(
        self.ctx.convert.list_type,
        {abstract_utils.T: abstract.TypeParameter(abstract_utils.T, self.ctx)},
        self.ctx,
    )
    for subst in self._match_var(left, right):
      self.assertSetEqual(set(subst), {abstract_utils.T})
      self.assertListEqual(
          subst[abstract_utils.T].data, [self.ctx.convert.empty]
      )

  def test_list_against_mapping(self):
    left = self._convert_type("list", as_instance=True)
    right = self.ctx.convert.lookup_value("typing", "Mapping")
    self.assertNoMatch(left, right)

  def test_list_against_parameterized_mapping(self):
    left = self._convert_type("list", as_instance=True)
    right = abstract.ParameterizedClass(
        self.ctx.convert.lookup_value("typing", "Mapping"),
        {
            abstract_utils.K: abstract.TypeParameter(
                abstract_utils.K, self.ctx
            ),
            abstract_utils.V: abstract.TypeParameter(
                abstract_utils.V, self.ctx
            ),
        },
        self.ctx,
    )
    self.assertNoMatch(left, right)


class TypeVarTest(MatcherTestBase):
  """Test matching TypeVar against various types."""

  def test_match_from_mro(self):
    # A TypeParameter never matches anything in match_from_mro, since its mro is
    # empty. This test is mostly to make sure we don't crash.
    self.assertIsNone(
        self.matcher.match_from_mro(
            abstract.TypeParameter("T", self.ctx), self.ctx.convert.int_type
        )
    )

  def test_compute_matches(self):
    x_val = abstract.TypeParameter("T", self.ctx)
    args = [
        types.Arg(
            name="x",
            value=x_val.to_variable(self.ctx.root_node),
            typ=self.ctx.convert.unsolvable,
        )
    ]
    (match,) = self.matcher.compute_matches(args, match_all_views=True)
    self.assertEqual(match.subst, {})

  def test_compute_matches_no_match(self):
    x_val = abstract.TypeParameter("T", self.ctx)
    args = [
        types.Arg(
            name="x",
            value=x_val.to_variable(self.ctx.root_node),
            typ=self.ctx.convert.int_type,
        )
    ]
    with self.assertRaises(error_types.MatchError):
      self.matcher.compute_matches(args, match_all_views=True)

  def test_compute_one_match(self):
    self.assertTrue(
        self.matcher.compute_one_match(
            abstract.TypeParameter("T", self.ctx).to_variable(
                self.ctx.root_node
            ),
            self.ctx.convert.unsolvable,
        ).success
    )

  def test_compute_one_match_no_match(self):
    self.assertFalse(
        self.matcher.compute_one_match(
            abstract.TypeParameter("T", self.ctx).to_variable(
                self.ctx.root_node
            ),
            self.ctx.convert.int_type,
        ).success
    )

  def test_any(self):
    self.assertMatch(
        abstract.TypeParameter("T", self.ctx), self.ctx.convert.unsolvable
    )

  def test_object(self):
    self.assertMatch(
        abstract.TypeParameter("T", self.ctx), self.ctx.convert.object_type
    )

  def test_type(self):
    self.assertMatch(
        abstract.TypeParameter("T", self.ctx), self.ctx.convert.type_type
    )

  def test_parameterized_type(self):
    self.assertMatch(
        abstract.TypeParameter("T", self.ctx),
        abstract.ParameterizedClass(
            self.ctx.convert.type_type,
            {abstract_utils.T: self.ctx.convert.unsolvable},
            self.ctx,
        ),
    )

  def test_parameterized_type_no_match(self):
    self.assertNoMatch(
        abstract.TypeParameter("T", self.ctx),
        abstract.ParameterizedClass(
            self.ctx.convert.type_type,
            {abstract_utils.T: self.ctx.convert.int_type},
            self.ctx,
        ),
    )

  def test_nested(self):
    self.assertMatch(
        abstract.ParameterizedClass(
            self.ctx.convert.list_type,
            {abstract_utils.T: abstract.TypeParameter("T", self.ctx)},
            self.ctx,
        ),
        self.ctx.convert.type_type,
    )

  def test_nested_no_match(self):
    self.assertNoMatch(
        abstract.ParameterizedClass(
            self.ctx.convert.list_type,
            {abstract_utils.T: abstract.TypeParameter("T", self.ctx)},
            self.ctx,
        ),
        self.ctx.convert.list_type,
    )

  def test_no_match(self):
    self.assertNoMatch(
        abstract.TypeParameter("T", self.ctx), self.ctx.convert.int_type
    )


if __name__ == "__main__":
  unittest.main()
