from typing import cast

from pytype.rewrite.abstract import classes
from pytype.rewrite.abstract import functions
from pytype.rewrite.tests import test_utils

import unittest


class ClassTest(test_utils.ContextfulTestBase):

  def test_get_attribute(self):
    x = self.ctx.consts[5]
    cls = classes.SimpleClass(self.ctx, 'X', {'x': x})
    self.assertEqual(cls.get_attribute('x'), x)

  def test_get_nonexistent_attribute(self):
    cls = classes.SimpleClass(self.ctx, 'X', {})
    self.assertIsNone(cls.get_attribute('x'))

  def test_get_parent_attribute(self):
    x = self.ctx.consts[5]
    parent = classes.SimpleClass(self.ctx, 'Parent', {'x': x})
    child = classes.SimpleClass(self.ctx, 'Child', {}, bases=[parent])
    self.assertEqual(child.get_attribute('x'), x)

  def test_instantiate(self):
    cls = classes.SimpleClass(self.ctx, 'X', {})
    instance = cls.instantiate()
    self.assertEqual(instance.cls, cls)

  def test_call(self):
    cls = classes.SimpleClass(self.ctx, 'X', {})
    instance = cls.call(functions.Args()).get_return_value()
    self.assertEqual(instance.cls, cls)

  def test_mro(self):
    parent = classes.SimpleClass(self.ctx, 'Parent', {})
    child = classes.SimpleClass(self.ctx, 'Child', {}, bases=[parent])
    self.assertEqual(child.mro(), [child, parent, self.ctx.types[object]])

  def test_metaclass(self):
    type_type = cast(classes.SimpleClass, self.ctx.types[type])
    meta = classes.SimpleClass(self.ctx, 'Meta', {}, bases=[type_type])
    cls = classes.SimpleClass(self.ctx, 'C', {}, keywords={'metaclass': meta})
    self.assertEqual(cls.metaclass, meta)

  def test_inherited_metaclass(self):
    type_type = cast(classes.SimpleClass, self.ctx.types[type])
    meta = classes.SimpleClass(self.ctx, 'Meta', {}, bases=[type_type])
    parent = classes.SimpleClass(self.ctx, 'Parent', {},
                                 keywords={'metaclass': meta})
    child = classes.SimpleClass(self.ctx, 'Child', {}, bases=[parent])
    self.assertEqual(child.metaclass, meta)


class MutableInstanceTest(test_utils.ContextfulTestBase):

  def test_get_instance_attribute(self):
    cls = classes.SimpleClass(self.ctx, 'X', {})
    instance = classes.MutableInstance(self.ctx, cls)
    instance.members['x'] = self.ctx.consts[3]
    self.assertEqual(instance.get_attribute('x'), self.ctx.consts[3])

  def test_get_class_attribute(self):
    cls = classes.SimpleClass(
        self.ctx, 'X', {'x': self.ctx.consts[3]})
    instance = classes.MutableInstance(self.ctx, cls)
    self.assertEqual(instance.get_attribute('x'), self.ctx.consts[3])

  def test_set_attribute(self):
    cls = classes.SimpleClass(self.ctx, 'X', {})
    instance = classes.MutableInstance(self.ctx, cls)
    instance.set_attribute('x', self.ctx.consts[3])
    self.assertEqual(instance.members['x'], self.ctx.consts[3])


class FrozenInstanceTest(test_utils.ContextfulTestBase):

  def test_get_attribute(self):
    cls = classes.SimpleClass(self.ctx, 'X', {})
    mutable_instance = classes.MutableInstance(self.ctx, cls)
    mutable_instance.set_attribute('x', self.ctx.consts[3])
    instance = mutable_instance.freeze()
    self.assertEqual(instance.get_attribute('x'), self.ctx.consts[3])


class ModuleTest(test_utils.ContextfulTestBase):

  def test_instance_attribute(self):
    attr = classes.Module(self.ctx, 'os').get_attribute('name')
    self.assertIsInstance(attr, classes.FrozenInstance)
    self.assertEqual(attr.cls.name, 'str')

  def test_class_attribute(self):
    attr = classes.Module(self.ctx, 'os').get_attribute('__name__')
    self.assertIsInstance(attr, classes.FrozenInstance)
    self.assertEqual(attr.cls.name, 'str')

  def test_submodule(self):
    attr = classes.Module(self.ctx, 'os').get_attribute('path')
    self.assertIsInstance(attr, classes.Module)
    self.assertEqual(attr.name, 'os.path')


if __name__ == '__main__':
  unittest.main()
