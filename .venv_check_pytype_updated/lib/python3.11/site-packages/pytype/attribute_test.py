"""Tests for attribute.py."""

from pytype import config
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.tests import test_base
from pytype.tests import test_utils

import unittest


def _get_origins(binding):
  """Gets all the bindings in the given binding's origins."""
  bindings = set()
  for origin in binding.origins:
    for source_set in origin.source_sets:
      bindings |= source_set
  return bindings


class ValselfTest(test_base.UnitTest):
  """Tests for get_attribute's `valself` parameter."""

  def setUp(self):
    super().setUp()
    options = config.Options.create(
        python_version=self.python_version, color="never"
    )
    self.ctx = test_utils.make_context(options)
    self.node = self.ctx.root_node
    self.attribute_handler = self.ctx.attribute_handler

  def test_instance_no_valself(self):
    instance = abstract.Instance(self.ctx.convert.int_type, self.ctx)
    _, attr_var = self.attribute_handler.get_attribute(
        self.node, instance, "real"
    )
    (attr_binding,) = attr_var.bindings
    self.assertEqual(attr_binding.data.cls, self.ctx.convert.int_type)
    # Since `valself` was not passed to get_attribute, a binding to
    # `instance` is not among the attribute's origins.
    self.assertNotIn(instance, [o.data for o in _get_origins(attr_binding)])

  def test_instance_with_valself(self):
    instance = abstract.Instance(self.ctx.convert.int_type, self.ctx)
    valself = instance.to_binding(self.node)
    _, attr_var = self.attribute_handler.get_attribute(
        self.node, instance, "real", valself
    )
    (attr_binding,) = attr_var.bindings
    self.assertEqual(attr_binding.data.cls, self.ctx.convert.int_type)
    # Since `valself` was passed to get_attribute, it is added to the
    # attribute's origins.
    self.assertIn(valself, _get_origins(attr_binding))

  def test_class_no_valself(self):
    meta_members = {"x": self.ctx.convert.none.to_variable(self.node)}
    meta = abstract.InterpreterClass(
        "M", [], meta_members, None, None, (), self.ctx
    )
    cls = abstract.InterpreterClass("X", [], {}, meta, None, (), self.ctx)
    _, attr_var = self.attribute_handler.get_attribute(self.node, cls, "x")
    # Since `valself` was not passed to get_attribute, we do not look at the
    # metaclass, so M.x is not returned.
    self.assertIsNone(attr_var)

  def test_class_with_instance_valself(self):
    meta_members = {"x": self.ctx.convert.none.to_variable(self.node)}
    meta = abstract.InterpreterClass(
        "M", [], meta_members, None, None, (), self.ctx
    )
    cls = abstract.InterpreterClass("X", [], {}, meta, None, (), self.ctx)
    valself = abstract.Instance(cls, self.ctx).to_binding(self.node)
    _, attr_var = self.attribute_handler.get_attribute(
        self.node, cls, "x", valself
    )
    # Since `valself` is an instance of X, we do not look at the metaclass, so
    # M.x is not returned.
    self.assertIsNone(attr_var)

  def test_class_with_class_valself(self):
    meta_members = {"x": self.ctx.convert.none.to_variable(self.node)}
    meta = abstract.InterpreterClass(
        "M", [], meta_members, None, None, (), self.ctx
    )
    cls = abstract.InterpreterClass("X", [], {}, meta, None, (), self.ctx)
    valself = cls.to_binding(self.node)
    _, attr_var = self.attribute_handler.get_attribute(
        self.node, cls, "x", valself
    )
    # Since `valself` is X itself, we look at the metaclass and return M.x.
    self.assertEqual(attr_var.data, [self.ctx.convert.none])

  def test_getitem_no_valself(self):
    cls = abstract.InterpreterClass("X", [], {}, None, None, (), self.ctx)
    _, attr_var = self.attribute_handler.get_attribute(
        self.node, cls, "__getitem__"
    )
    (attr,) = attr_var.data
    # Since we looked up __getitem__ on a class without passing in `valself`,
    # the class is treated as an annotation.
    self.assertIs(attr.func.__func__, abstract.AnnotationClass.getitem_slot)

  def test_getitem_with_instance_valself(self):
    cls = abstract.InterpreterClass("X", [], {}, None, None, (), self.ctx)
    valself = abstract.Instance(cls, self.ctx).to_binding(self.node)
    _, attr_var = self.attribute_handler.get_attribute(
        self.node, cls, "__getitem__", valself
    )
    # Since we passed in `valself` for this lookup of __getitem__ on a class,
    # it is treated as a normal lookup; X.__getitem__ does not exist.
    self.assertIsNone(attr_var)

  def test_getitem_with_class_valself(self):
    cls = abstract.InterpreterClass("X", [], {}, None, None, (), self.ctx)
    valself = cls.to_binding(self.node)
    _, attr_var = self.attribute_handler.get_attribute(
        self.node, cls, "__getitem__", valself
    )
    # Since we passed in `valself` for this lookup of __getitem__ on a class,
    # it is treated as a normal lookup; X.__getitem__ does not exist.
    self.assertIsNone(attr_var)


class AttributeTest(test_base.UnitTest):

  def setUp(self):
    super().setUp()
    options = config.Options.create(python_version=self.python_version)
    self._ctx = test_utils.make_context(options)

  def test_type_parameter_instance(self):
    t = abstract.TypeParameter(abstract_utils.T, self._ctx)
    t_instance = abstract.TypeParameterInstance(
        t, self._ctx.convert.primitive_instances[str], self._ctx
    )
    node, var = self._ctx.attribute_handler.get_attribute(
        self._ctx.root_node, t_instance, "upper"
    )
    self.assertIs(node, self._ctx.root_node)
    (attr,) = var.data
    self.assertIsInstance(attr, abstract.PyTDFunction)

  def test_type_parameter_instance_bad_attribute(self):
    t = abstract.TypeParameter(abstract_utils.T, self._ctx)
    t_instance = abstract.TypeParameterInstance(
        t, self._ctx.convert.primitive_instances[str], self._ctx
    )
    node, var = self._ctx.attribute_handler.get_attribute(
        self._ctx.root_node, t_instance, "rumpelstiltskin"
    )
    self.assertIs(node, self._ctx.root_node)
    self.assertIsNone(var)

  def test_empty_type_parameter_instance(self):
    t = abstract.TypeParameter(
        abstract_utils.T, self._ctx, bound=self._ctx.convert.int_type
    )
    instance = abstract.Instance(self._ctx.convert.list_type, self._ctx)
    t_instance = abstract.TypeParameterInstance(t, instance, self._ctx)
    node, var = self._ctx.attribute_handler.get_attribute(
        self._ctx.root_node, t_instance, "real"
    )
    self.assertIs(node, self._ctx.root_node)
    (attr,) = var.data
    self.assertIs(attr, self._ctx.convert.primitive_instances[int])

  def test_type_parameter_instance_set_attribute(self):
    t = abstract.TypeParameter(abstract_utils.T, self._ctx)
    t_instance = abstract.TypeParameterInstance(
        t, self._ctx.convert.primitive_instances[str], self._ctx
    )
    node = self._ctx.attribute_handler.set_attribute(
        self._ctx.root_node,
        t_instance,
        "rumpelstiltskin",
        self._ctx.new_unsolvable(self._ctx.root_node),
    )
    self.assertIs(node, self._ctx.root_node)
    self.assertEqual(
        str(self._ctx.errorlog).strip(),
        "Can't assign attribute 'rumpelstiltskin' on str [not-writable]",
    )

  def test_union_set_attribute(self):
    list_instance = abstract.Instance(self._ctx.convert.list_type, self._ctx)
    cls = abstract.InterpreterClass("obj", [], {}, None, None, (), self._ctx)
    cls_instance = abstract.Instance(cls, self._ctx)
    union = abstract.Union([cls_instance, list_instance], self._ctx)
    node = self._ctx.attribute_handler.set_attribute(
        self._ctx.root_node,
        union,
        "rumpelstiltskin",
        self._ctx.convert.none_type.to_variable(self._ctx.root_node),
    )
    self.assertEqual(
        cls_instance.members["rumpelstiltskin"].data.pop(),
        self._ctx.convert.none_type,
    )
    self.assertIs(node, self._ctx.root_node)
    (error,) = self._ctx.errorlog.unique_sorted_errors()
    self.assertEqual(error.name, "not-writable")


if __name__ == "__main__":
  unittest.main()
