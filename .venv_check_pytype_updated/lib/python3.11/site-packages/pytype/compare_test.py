"""Tests for compare.py."""

from pytype import compare
from pytype import config
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.errors import error_types
from pytype.pytd import pytd
from pytype.pytd import slots
from pytype.tests import test_base
from pytype.tests import test_utils

import unittest


class CompareTestBase(test_base.UnitTest):

  def setUp(self):
    super().setUp()
    options = config.Options.create(python_version=self.python_version)
    self._ctx = test_utils.make_context(options)
    self._program = self._ctx.program
    self._node = self._ctx.root_node.ConnectNew("test_node")
    self._convert = self._ctx.convert

  def assertTruthy(self, value):
    self.assertIs(True, compare.compatible_with(value, True))
    self.assertIs(False, compare.compatible_with(value, False))

  def assertFalsy(self, value):
    self.assertIs(False, compare.compatible_with(value, True))
    self.assertIs(True, compare.compatible_with(value, False))

  def assertAmbiguous(self, value):
    self.assertIs(True, compare.compatible_with(value, True))
    self.assertIs(True, compare.compatible_with(value, False))


class InstanceTest(CompareTestBase):

  def test_compatible_with_object(self):
    # object() is not compatible with False
    i = abstract.Instance(self._convert.object_type, self._ctx)
    self.assertTruthy(i)

  def test_compatible_with_numeric(self):
    # Numbers can evaluate to True or False
    i = abstract.Instance(self._convert.int_type, self._ctx)
    self.assertAmbiguous(i)

  def test_compatible_with_list(self):
    i = abstract.List([], self._ctx)
    # Empty list is not compatible with True.
    self.assertFalsy(i)
    # Once a type parameter is set, list is compatible with True and False.
    i.merge_instance_type_parameter(
        self._node,
        abstract_utils.T,
        self._convert.object_type.to_variable(self._ctx.root_node),
    )
    self.assertAmbiguous(i)

  def test_compatible_with_set(self):
    i = abstract.Instance(self._convert.set_type, self._ctx)
    # Empty set is not compatible with True.
    self.assertFalsy(i)
    # Once a type parameter is set, list is compatible with True and False.
    i.merge_instance_type_parameter(
        self._node,
        abstract_utils.T,
        self._convert.object_type.to_variable(self._ctx.root_node),
    )
    self.assertAmbiguous(i)

  def test_compatible_with_none(self):
    # This test is specifically for abstract.Instance, so we don't use
    # self._convert.none, which is a ConcreteValue.
    i = abstract.Instance(self._convert.none_type, self._ctx)
    self.assertFalsy(i)

  def test_compare_frozensets(self):
    """Test that two frozensets can be compared for equality."""
    fset = self._convert.frozenset_type
    i = abstract.Instance(fset, self._ctx)
    j = abstract.Instance(fset, self._ctx)
    self.assertIs(None, compare.cmp_rel(self._ctx, slots.EQ, i, j))


class TupleTest(CompareTestBase):

  def setUp(self):
    super().setUp()
    self._var = self._program.NewVariable()
    self._var.AddBinding(abstract.Unknown(self._ctx), [], self._node)

  def test_compatible_with__not_empty(self):
    t = self._ctx.convert.tuple_to_value((self._var,))
    self.assertTruthy(t)

  def test_compatible_with__empty(self):
    t = self._ctx.convert.tuple_to_value(())
    self.assertFalsy(t)

  def test_getitem__concrete_index(self):
    t = self._ctx.convert.tuple_to_value((self._var,))
    index = self._convert.constant_to_var(0)
    node, var = t.cls.getitem_slot(self._node, index)
    self.assertIs(node, self._node)
    self.assertIs(
        abstract_utils.get_atomic_value(var),
        abstract_utils.get_atomic_value(self._var),
    )

  def test_getitem__abstract_index(self):
    t = self._ctx.convert.tuple_to_value((self._var,))
    index = self._convert.build_int(self._node)
    node, var = t.cls.getitem_slot(self._node, index)
    self.assertIs(node, self._node)
    self.assertIs(
        abstract_utils.get_atomic_value(var),
        abstract_utils.get_atomic_value(self._var),
    )

  def test_cmp_rel__equal(self):
    tup = self._convert.constant_to_value((3, 1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.LT, tup, tup))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.LE, tup, tup))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.EQ, tup, tup))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.NE, tup, tup))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.GE, tup, tup))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.GT, tup, tup))

  def test_cmp_rel__not_equal(self):
    tup1 = self._convert.constant_to_value((3, 1))
    tup2 = self._convert.constant_to_value((3, 5))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.LT, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.LT, tup2, tup1))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.LE, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.LE, tup2, tup1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.EQ, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.EQ, tup2, tup1))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.NE, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.NE, tup2, tup1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.GE, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.GE, tup2, tup1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.GT, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.GT, tup2, tup1))

  def test_cmp_rel__unknown(self):
    tup1 = self._convert.constant_to_value((3, 1))
    tup2 = abstract.Instance(self._convert.tuple_type, self._ctx)
    for op in (slots.LT, slots.LE, slots.EQ, slots.NE, slots.GE, slots.GT):
      self.assertIsNone(compare.cmp_rel(self._ctx, op, tup1, tup2))
      self.assertIsNone(compare.cmp_rel(self._ctx, op, tup2, tup1))

  def test_cmp_rel__prefix_equal(self):
    tup1 = self._ctx.convert.tuple_to_value((
        self._convert.constant_to_value(3).to_variable(self._node),
        self._convert.constant_to_value(1).to_variable(self._node),
        self._convert.primitive_instances[int].to_variable(self._node),
    ))
    tup2 = self._convert.constant_to_value((3, 1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.LT, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.LT, tup2, tup1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.LE, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.LE, tup2, tup1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.EQ, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.EQ, tup2, tup1))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.NE, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.NE, tup2, tup1))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.GE, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.GE, tup2, tup1))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.GT, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.GT, tup2, tup1))

  def test_cmp_rel__prefix_not_equal(self):
    tup1 = self._ctx.convert.tuple_to_value((
        self._convert.constant_to_value(3).to_variable(self._node),
        self._convert.constant_to_value(1).to_variable(self._node),
        self._convert.primitive_instances[int].to_variable(self._node),
    ))
    tup2 = self._convert.constant_to_value((4, 2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.LT, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.LT, tup2, tup1))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.LE, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.LE, tup2, tup1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.EQ, tup1, tup2))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.EQ, tup2, tup1))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.NE, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.NE, tup2, tup1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.GE, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.GE, tup2, tup1))
    self.assertIs(False, compare.cmp_rel(self._ctx, slots.GT, tup1, tup2))
    self.assertIs(True, compare.cmp_rel(self._ctx, slots.GT, tup2, tup1))

  def test_cmp_rel__prefix_unknown(self):
    tup1 = self._ctx.convert.tuple_to_value((
        self._convert.constant_to_value(3).to_variable(self._node),
        self._convert.primitive_instances[int].to_variable(self._node),
    ))
    tup2 = self._convert.constant_to_value((3, 1))
    for op in (slots.LT, slots.LE, slots.EQ, slots.NE, slots.GE, slots.GT):
      self.assertIsNone(compare.cmp_rel(self._ctx, op, tup1, tup2))
      self.assertIsNone(compare.cmp_rel(self._ctx, op, tup2, tup1))


class DictTest(CompareTestBase):

  def setUp(self):
    super().setUp()
    self._d = abstract.Dict(self._ctx)
    self._var = self._program.NewVariable()
    self._var.AddBinding(abstract.Unknown(self._ctx), [], self._node)

  def test_compatible_with__when_empty(self):
    self.assertFalsy(self._d)

  def test_compatible_with__after_setitem(self):
    # Once a slot is added, dict is ambiguous.
    self._d.setitem_slot(self._node, self._var, self._var)
    self.assertAmbiguous(self._d)

  def test_compatible_with__after_set_str_item(self):
    self._d.set_str_item(self._node, "key", self._var)
    self.assertTruthy(self._d)

  def test_compatible_with__after_unknown_update(self):
    # Updating an empty dict with an unknown value makes the former ambiguous.
    self._d.update(self._node, abstract.Unknown(self._ctx))
    self.assertAmbiguous(self._d)

  def test_compatible_with__after_empty_update(self):
    empty_dict = abstract.Dict(self._ctx)
    self._d.update(self._node, empty_dict)
    self.assertFalsy(self._d)

  def test_compatible_with__after_unambiguous_update(self):
    unambiguous_dict = abstract.Dict(self._ctx)
    unambiguous_dict.set_str_item(
        self._node, "a", self._ctx.new_unsolvable(self._node)
    )
    self._d.update(self._node, unambiguous_dict)
    self.assertTruthy(self._d)

  def test_compatible_with__after_ambiguous_update(self):
    ambiguous_dict = abstract.Dict(self._ctx)
    ambiguous_dict.merge_instance_type_parameter(
        self._node, abstract_utils.K, self._ctx.new_unsolvable(self._node)
    )
    ambiguous_dict.is_concrete = False
    self._d.update(self._node, ambiguous_dict)
    self.assertAmbiguous(self._d)

  def test_compatible_with__after_concrete_update(self):
    self._d.update(self._node, {})
    self.assertFalsy(self._d)
    self._d.update(self._node, {"a": self._ctx.new_unsolvable(self._node)})
    self.assertTruthy(self._d)

  def test_pop(self):
    self._d.set_str_item(self._node, "a", self._var)
    node, ret = self._d.pop_slot(
        self._node, self._convert.build_string(self._node, "a")
    )
    self.assertFalsy(self._d)
    self.assertIs(node, self._node)
    self.assertIs(ret, self._var)

  def test_pop_with_default(self):
    self._d.set_str_item(self._node, "a", self._var)
    node, ret = self._d.pop_slot(
        self._node,
        self._convert.build_string(self._node, "a"),
        self._convert.none.to_variable(self._node),
    )  # default is ignored
    self.assertFalsy(self._d)
    self.assertIs(node, self._node)
    self.assertIs(ret, self._var)

  def test_bad_pop(self):
    self._d.set_str_item(self._node, "a", self._var)
    self.assertRaises(
        error_types.DictKeyMissing,
        self._d.pop_slot,
        self._node,
        self._convert.build_string(self._node, "b"),
    )
    self.assertTruthy(self._d)

  def test_bad_pop_with_default(self):
    val = self._convert.primitive_instances[int]
    self._d.set_str_item(self._node, "a", val.to_variable(self._node))
    node, ret = self._d.pop_slot(
        self._node,
        self._convert.build_string(self._node, "b"),
        self._convert.none.to_variable(self._node),
    )
    self.assertTruthy(self._d)
    self.assertIs(node, self._node)
    self.assertListEqual(ret.data, [self._convert.none])

  def test_ambiguous_pop(self):
    val = self._convert.primitive_instances[int]
    self._d.set_str_item(self._node, "a", val.to_variable(self._node))
    ambiguous_key = self._convert.primitive_instances[str]
    node, ret = self._d.pop_slot(
        self._node, ambiguous_key.to_variable(self._node)
    )
    self.assertAmbiguous(self._d)
    self.assertIs(node, self._node)
    self.assertListEqual(ret.data, [val])

  def test_ambiguous_pop_with_default(self):
    val = self._convert.primitive_instances[int]
    self._d.set_str_item(self._node, "a", val.to_variable(self._node))
    ambiguous_key = self._convert.primitive_instances[str]
    default_var = self._convert.none.to_variable(self._node)
    node, ret = self._d.pop_slot(
        self._node, ambiguous_key.to_variable(self._node), default_var
    )
    self.assertAmbiguous(self._d)
    self.assertIs(node, self._node)
    self.assertSetEqual(set(ret.data), {val, self._convert.none})

  def test_ambiguous_dict_after_pop(self):
    ambiguous_key = self._convert.primitive_instances[str]
    val = self._convert.primitive_instances[int]
    node, _ = self._d.setitem_slot(
        self._node,
        ambiguous_key.to_variable(self._node),
        val.to_variable(self._node),
    )
    _, ret = self._d.pop_slot(node, self._convert.build_string(node, "a"))
    self.assertAmbiguous(self._d)
    self.assertListEqual(ret.data, [val])

  def test_ambiguous_dict_after_pop_with_default(self):
    ambiguous_key = self._convert.primitive_instances[str]
    val = self._convert.primitive_instances[int]
    node, _ = self._d.setitem_slot(
        self._node,
        ambiguous_key.to_variable(self._node),
        val.to_variable(self._node),
    )
    _, ret = self._d.pop_slot(
        node,
        self._convert.build_string(node, "a"),
        self._convert.none.to_variable(node),
    )
    self.assertAmbiguous(self._d)
    self.assertSetEqual(set(ret.data), {val, self._convert.none})


class FunctionTest(CompareTestBase):

  def test_compatible_with(self):
    pytd_sig = pytd.Signature((), None, None, pytd.AnythingType(), (), ())
    sig = abstract.PyTDSignature("f", pytd_sig, self._ctx)
    f = abstract.PyTDFunction(
        "f", (sig,), pytd.MethodKind.METHOD, (), self._ctx
    )
    self.assertTruthy(f)


class ClassTest(CompareTestBase):

  def test_compatible_with(self):
    cls = abstract.InterpreterClass("X", [], {}, None, None, (), self._ctx)
    self.assertTruthy(cls)


if __name__ == "__main__":
  unittest.main()
