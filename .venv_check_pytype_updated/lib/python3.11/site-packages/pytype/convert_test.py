"""Tests for convert.py."""

from pytype import config
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.tests import test_base
from pytype.tests import test_utils

import unittest


class ConvertTest(test_base.UnitTest):

  def setUp(self):
    super().setUp()
    options = config.Options.create(python_version=self.python_version)
    self._ctx = test_utils.make_context(options)

  def _load_ast(self, name, src):
    with test_utils.Tempdir() as d:
      d.create_file(name + ".pyi", src)
      self._ctx.options.tweak(pythonpath=[d.path])  # monkeypatch
      return self._ctx.loader.import_name(name)

  def _convert_class(self, name, ast):
    return self._ctx.convert.constant_to_value(
        ast.Lookup(name), {}, self._ctx.root_node
    )

  def test_convert_metaclass(self):
    ast = self._load_ast(
        "a",
        """
      class A(type): ...
      class B(metaclass=A): ...
      class C(B): ...
    """,
    )
    meta = self._convert_class("a.A", ast)
    cls_meta = self._convert_class("a.B", ast).cls
    subcls_meta = self._convert_class("a.C", ast).cls
    self.assertEqual(meta, cls_meta)
    self.assertEqual(meta, subcls_meta)

  def test_convert_default_metaclass(self):
    ast = self._load_ast(
        "a",
        """
      class A: ...
    """,
    )
    cls = self._convert_class("a.A", ast)
    self.assertEqual(cls.cls, self._ctx.convert.type_type)

  def test_convert_metaclass_with_generic(self):
    ast = self._load_ast(
        "a",
        """
      from typing import Generic, TypeVar
      T = TypeVar("T")
      class A(type): ...
      class B(Generic[T], metaclass=A): ...
      class C(B[int]): ...
    """,
    )
    meta = self._convert_class("a.A", ast)
    cls_meta = self._convert_class("a.B", ast).cls
    subcls_meta = self._convert_class("a.C", ast).cls
    self.assertEqual(meta, cls_meta)
    self.assertEqual(meta, subcls_meta)

  def test_generic_with_any_param(self):
    ast = self._load_ast(
        "a",
        """
      from typing import Dict
      x = ...  # type: Dict[str]
    """,
    )
    val = self._ctx.convert.constant_to_value(
        ast.Lookup("a.x").type, {}, self._ctx.root_node
    )
    self.assertIs(
        val.formal_type_parameters[abstract_utils.K], self._ctx.convert.str_type
    )
    self.assertIs(
        val.formal_type_parameters[abstract_utils.V],
        self._ctx.convert.unsolvable,
    )

  def test_convert_long(self):
    val = self._ctx.convert.constant_to_value(2**64, {}, self._ctx.root_node)
    self.assertIs(val, self._ctx.convert.primitive_instances[int])

  def test_heterogeneous_tuple(self):
    ast = self._load_ast(
        "a",
        """
      from typing import Tuple
      x = ...  # type: Tuple[str, int]
    """,
    )
    x = ast.Lookup("a.x").type
    cls = self._ctx.convert.constant_to_value(x, {}, self._ctx.root_node)
    instance = self._ctx.convert.constant_to_value(
        abstract_utils.AsInstance(x), {}, self._ctx.root_node
    )
    self.assertIsInstance(cls, abstract.TupleClass)
    self.assertCountEqual(
        cls.formal_type_parameters.items(),
        [
            (0, self._ctx.convert.str_type),
            (1, self._ctx.convert.int_type),
            (
                abstract_utils.T,
                abstract.Union(
                    [
                        cls.formal_type_parameters[0],
                        cls.formal_type_parameters[1],
                    ],
                    self._ctx,
                ),
            ),
        ],
    )
    self.assertIsInstance(instance, abstract.Tuple)
    self.assertListEqual(
        [v.data for v in instance.pyval],
        [
            [self._ctx.convert.primitive_instances[str]],
            [self._ctx.convert.primitive_instances[int]],
        ],
    )
    # The order of option elements in Union is random
    self.assertCountEqual(
        instance.get_instance_type_parameter(abstract_utils.T).data,
        [
            self._ctx.convert.primitive_instances[str],
            self._ctx.convert.primitive_instances[int],
        ],
    )

  def test_build_bool(self):
    any_bool = self._ctx.convert.build_bool(self._ctx.root_node, None)
    t_bool = self._ctx.convert.build_bool(self._ctx.root_node, True)
    f_bool = self._ctx.convert.build_bool(self._ctx.root_node, False)
    self.assertEqual(
        any_bool.data, [self._ctx.convert.primitive_instances[bool]]
    )
    self.assertEqual(t_bool.data, [self._ctx.convert.true])
    self.assertEqual(f_bool.data, [self._ctx.convert.false])

  def test_boolean_constants(self):
    true = self._ctx.convert.constant_to_value(True, {}, self._ctx.root_node)
    self.assertEqual(true, self._ctx.convert.true)
    false = self._ctx.convert.constant_to_value(False, {}, self._ctx.root_node)
    self.assertEqual(false, self._ctx.convert.false)

  def test_callable_with_args(self):
    ast = self._load_ast(
        "a",
        """
      from typing import Callable
      x = ...  # type: Callable[[int, bool], str]
    """,
    )
    x = ast.Lookup("a.x").type
    cls = self._ctx.convert.constant_to_value(x, {}, self._ctx.root_node)
    instance = self._ctx.convert.constant_to_value(
        abstract_utils.AsInstance(x), {}, self._ctx.root_node
    )
    self.assertIsInstance(cls, abstract.CallableClass)
    self.assertCountEqual(
        cls.formal_type_parameters.items(),
        [
            (0, self._ctx.convert.int_type),
            (1, self._ctx.convert.primitive_classes[bool]),
            (
                abstract_utils.ARGS,
                abstract.Union(
                    [
                        cls.formal_type_parameters[0],
                        cls.formal_type_parameters[1],
                    ],
                    self._ctx,
                ),
            ),
            (abstract_utils.RET, self._ctx.convert.str_type),
        ],
    )
    self.assertIsInstance(instance, abstract.Instance)
    self.assertEqual(instance.cls, cls)
    self.assertCountEqual(
        [
            (name, set(var.data))
            for name, var in instance.instance_type_parameters.items()
        ],
        [
            (
                abstract_utils.full_type_name(instance, abstract_utils.ARGS),
                {
                    self._ctx.convert.primitive_instances[int],
                    self._ctx.convert.primitive_instances[bool],
                },
            ),
            (
                abstract_utils.full_type_name(instance, abstract_utils.RET),
                {self._ctx.convert.primitive_instances[str]},
            ),
        ],
    )

  def test_callable_no_args(self):
    ast = self._load_ast(
        "a",
        """
      from typing import Any, Callable
      x = ... # type: Callable[[], Any]
    """,
    )
    x = ast.Lookup("a.x").type
    cls = self._ctx.convert.constant_to_value(x, {}, self._ctx.root_node)
    instance = self._ctx.convert.constant_to_value(
        abstract_utils.AsInstance(x), {}, self._ctx.root_node
    )
    self.assertIsInstance(
        cls.get_formal_type_parameter(abstract_utils.ARGS), abstract.Empty
    )
    self.assertEqual(
        abstract_utils.get_atomic_value(
            instance.get_instance_type_parameter(abstract_utils.ARGS)
        ),
        self._ctx.convert.empty,
    )

  def test_plain_callable(self):
    ast = self._load_ast(
        "a",
        """
      from typing import Callable
      x = ...  # type: Callable[..., int]
    """,
    )
    x = ast.Lookup("a.x").type
    cls = self._ctx.convert.constant_to_value(x, {}, self._ctx.root_node)
    instance = self._ctx.convert.constant_to_value(
        abstract_utils.AsInstance(x), {}, self._ctx.root_node
    )
    self.assertIsInstance(cls, abstract.ParameterizedClass)
    self.assertCountEqual(
        cls.formal_type_parameters.items(),
        [
            (abstract_utils.ARGS, self._ctx.convert.unsolvable),
            (abstract_utils.RET, self._ctx.convert.int_type),
        ],
    )
    self.assertIsInstance(instance, abstract.Instance)
    self.assertEqual(instance.cls, cls.base_cls)
    self.assertCountEqual(
        [
            (name, var.data)
            for name, var in instance.instance_type_parameters.items()
        ],
        [
            (
                abstract_utils.full_type_name(instance, abstract_utils.ARGS),
                [self._ctx.convert.unsolvable],
            ),
            (
                abstract_utils.full_type_name(instance, abstract_utils.RET),
                [self._ctx.convert.primitive_instances[int]],
            ),
        ],
    )

  def test_function_with_starargs(self):
    ast = self._load_ast(
        "a",
        """
      def f(*args: int): ...
    """,
    )
    f = self._ctx.convert.constant_to_value(
        ast.Lookup("a.f"), {}, self._ctx.root_node
    )
    (sig,) = f.signatures
    annot = sig.signature.annotations["args"]
    self.assertEqual(
        pytd_utils.Print(annot.to_pytd_type_of_instance()), "tuple[int, ...]"
    )

  def test_function_with_starstarargs(self):
    ast = self._load_ast(
        "a",
        """
      def f(**kwargs: int): ...
    """,
    )
    f = self._ctx.convert.constant_to_value(
        ast.Lookup("a.f"), {}, self._ctx.root_node
    )
    (sig,) = f.signatures
    annot = sig.signature.annotations["kwargs"]
    self.assertEqual(
        pytd_utils.Print(annot.to_pytd_type_of_instance()), "dict[str, int]"
    )

  def test_mro(self):
    ast = self._load_ast(
        "a",
        """
      x = ...  # type: dict
    """,
    )
    x = ast.Lookup("a.x").type
    cls = self._ctx.convert.constant_to_value(x, {}, self._ctx.root_node)
    self.assertListEqual(
        [v.name for v in cls.mro],
        [
            "dict",
            "Dict",
            "MutableMapping",
            "Mapping",
            "Sized",
            "Iterable",
            "Container",
            "Generic",
            "Protocol",
            "object",
        ],
    )

  def test_widen_type(self):
    ast = self._load_ast(
        "a",
        """
      x = ...  # type: tuple[int, ...]
      y = ...  # type: dict[str, int]
    """,
    )
    x = ast.Lookup("a.x").type
    tup = self._ctx.convert.constant_to_value(x, {}, self._ctx.root_node)
    widened_tup = self._ctx.convert.widen_type(tup)
    self.assertEqual(
        pytd_utils.Print(widened_tup.to_pytd_type_of_instance()),
        "Iterable[int]",
    )
    y = ast.Lookup("a.y").type
    dct = self._ctx.convert.constant_to_value(y, {}, self._ctx.root_node)
    widened_dct = self._ctx.convert.widen_type(dct)
    self.assertEqual(
        pytd_utils.Print(widened_dct.to_pytd_type_of_instance()),
        "Mapping[str, int]",
    )

  def test_abstract_method_round_trip(self):
    sig = pytd.Signature((), None, None, pytd.AnythingType(), (), ())
    f_pytd = pytd.Function(
        name="f",
        signatures=(sig,),
        kind=pytd.MethodKind.METHOD,
        flags=pytd.MethodFlag.ABSTRACT,
    )
    f = self._ctx.convert.constant_to_value(f_pytd, {}, self._ctx.root_node)
    self.assertTrue(f.is_abstract)
    f_out = f.to_pytd_def(self._ctx.root_node, f.name)
    self.assertTrue(f_out.is_abstract)

  def test_class_abstract_method(self):
    ast = self._load_ast(
        "a",
        """
      class A:
        @abstractmethod
        def f(self) -> int: ...
    """,
    )
    cls = self._ctx.convert.constant_to_value(
        ast.Lookup("a.A"), {}, self._ctx.root_node
    )
    self.assertCountEqual(cls.abstract_methods, {"f"})

  def test_class_inherited_abstract_method(self):
    ast = self._load_ast(
        "a",
        """
      class A:
        @abstractmethod
        def f(self) -> int: ...
      class B(A): ...
    """,
    )
    cls = self._ctx.convert.constant_to_value(
        ast.Lookup("a.B"), {}, self._ctx.root_node
    )
    self.assertCountEqual(cls.abstract_methods, {"f"})

  def test_class_override_abstract_method(self):
    ast = self._load_ast(
        "a",
        """
      class A:
        @abstractmethod
        def f(self) -> int: ...
      class B(A):
        def f(self) -> bool: ...
    """,
    )
    cls = self._ctx.convert.constant_to_value(
        ast.Lookup("a.B"), {}, self._ctx.root_node
    )
    self.assertFalse(cls.abstract_methods)

  def test_class_override_abstract_method_still_abstract(self):
    ast = self._load_ast(
        "a",
        """
      class A:
        @abstractmethod
        def f(self) -> int: ...
      class B(A):
        @abstractmethod
        def f(self) -> bool: ...
    """,
    )
    cls = self._ctx.convert.constant_to_value(
        ast.Lookup("a.B"), {}, self._ctx.root_node
    )
    self.assertCountEqual(cls.abstract_methods, {"f"})

  def test_parameterized_class_abstract_method(self):
    ast = self._load_ast(
        "a",
        """
      class A:
        @abstractmethod
        def f(self) -> int: ...
    """,
    )
    cls = self._ctx.convert.constant_to_value(
        ast.Lookup("a.A"), {}, self._ctx.root_node
    )
    parameterized_cls = abstract.ParameterizedClass(cls, {}, self._ctx)
    self.assertCountEqual(parameterized_cls.abstract_methods, {"f"})

  def test_classvar(self):
    ast = self._load_ast(
        "a",
        """
      from typing import ClassVar
      class X:
        v: ClassVar[int]
    """,
    )
    pyval = ast.Lookup("a.X").Lookup("v").type
    v = self._ctx.convert.constant_to_value(pyval, {}, self._ctx.root_node)
    self.assertEqual(v, self._ctx.convert.int_type)

  def test_classvar_instance(self):
    ast = self._load_ast(
        "a",
        """
      from typing import ClassVar
      class X:
        v: ClassVar[int]
    """,
    )
    pyval = ast.Lookup("a.X").Lookup("v").type
    v = self._ctx.convert.constant_to_value(
        abstract_utils.AsInstance(pyval), {}, self._ctx.root_node
    )
    self.assertEqual(v, self._ctx.convert.primitive_instances[int])

  def test_constant_name(self):
    # Test that we create a string name without crashing.
    self.assertIsInstance(self._ctx.convert.constant_name(int), str)
    self.assertIsInstance(self._ctx.convert.constant_name(None), str)
    self.assertIsInstance(
        self._ctx.convert.constant_name((int, (str, super))), str
    )

  def test_literal(self):
    ast = self._load_ast(
        "a",
        r"""
      from typing import Literal
      one: Literal[1] = ...
      abc: Literal["abc"] = ...
      null: Literal["\0"] = ...
    """,
    )
    with self.subTest("one"):
      pyval = ast.Lookup("a.one")
      literal = self._ctx.convert._get_literal_value(pyval.type, None)
      self.assertEqual(literal.value.pyval, 1)
    with self.subTest("abc"):
      pyval = ast.Lookup("a.abc")
      literal = self._ctx.convert._get_literal_value(pyval.type, None)
      self.assertEqual(literal.value.pyval, "abc")
    with self.subTest("null"):
      pyval = ast.Lookup("a.null")
      literal = self._ctx.convert._get_literal_value(pyval.type, None)
      self.assertEqual(literal.value.pyval, "\0")


if __name__ == "__main__":
  unittest.main()
