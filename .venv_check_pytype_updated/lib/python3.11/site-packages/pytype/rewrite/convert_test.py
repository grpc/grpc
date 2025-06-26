from pytype.pytd import pytd
from pytype.rewrite.abstract import abstract
from pytype.rewrite.tests import test_utils

import unittest


class ConverterTestBase(test_utils.PytdTestBase, test_utils.ContextfulTestBase):

  def setUp(self):
    super().setUp()
    self.conv = self.ctx.abstract_converter


class PytdTypeToValueTest(ConverterTestBase):

  def test_class_type(self):
    pytd_class = self.build_pytd('class X: ...')
    pytd_class_type = pytd.ClassType(name='X', cls=pytd_class)
    abstract_class = self.conv.pytd_type_to_value(pytd_class_type)
    self.assertIsInstance(abstract_class, abstract.SimpleClass)
    self.assertEqual(abstract_class.name, 'X')

  def test_anything_type(self):
    abstract_value = self.conv.pytd_type_to_value(pytd.AnythingType())
    self.assertEqual(abstract_value, self.ctx.consts.singles['Any'])

  def test_nothing_type(self):
    abstract_value = self.conv.pytd_type_to_value(pytd.NothingType())
    self.assertEqual(abstract_value, self.ctx.consts.singles['Never'])


class PytdFunctionToValueTest(ConverterTestBase):

  def test_basic(self):
    pytd_func = self.build_pytd("""
      from typing import Any
      def f(x: Any) -> Any: ...
    """)
    func = self.conv.pytd_function_to_value(pytd_func)
    self.assertIsInstance(func, abstract.PytdFunction)
    self.assertEqual(func.name, 'f')
    self.assertEqual(func.module, '<test>')
    self.assertEqual(len(func.signatures), 1)
    self.assertEqual(repr(func.signatures[0]), 'def f(x: Any) -> Any')


class PytdClassToValueTest(ConverterTestBase):

  def test_empty_body(self):
    pytd_cls = self.build_pytd('class C: ...')
    cls = self.conv.pytd_class_to_value(pytd_cls)
    self.assertEqual(cls.name, 'C')
    self.assertEqual(cls.module, '<test>')
    self.assertFalse(cls.members)

  def test_method(self):
    pytd_cls = self.build_pytd("""
      class C:
        def f(self, x) -> None: ...
    """)
    cls = self.conv.pytd_class_to_value(pytd_cls)
    self.assertEqual(cls.name, 'C')
    self.assertEqual(set(cls.members), {'f'})
    f = cls.members['f']
    self.assertIsInstance(f, abstract.PytdFunction)
    self.assertEqual(f.module, '<test>')
    self.assertEqual(repr(f.signatures[0]), 'def C.f(self: C, x: Any) -> None')

  def test_constant(self):
    pytd_cls = self.build_pytd("""
      class C:
        CONST: int
    """)
    cls = self.conv.pytd_class_to_value(pytd_cls)
    self.assertEqual(cls.name, 'C')
    self.assertEqual(set(cls.members), {'CONST'})
    const = cls.members['CONST']
    self.assertIsInstance(const, abstract.FrozenInstance)
    self.assertEqual(const.cls.name, 'int')

  def test_nested_class(self):
    pytd_cls = self.build_pytd("""
      class C:
        class D: ...
    """)
    cls = self.conv.pytd_class_to_value(pytd_cls)
    self.assertEqual(cls.name, 'C')
    self.assertEqual(set(cls.members), {'D'})
    nested_class = cls.members['D']
    self.assertIsInstance(nested_class, abstract.SimpleClass)
    self.assertEqual(nested_class.name, 'D')

  def test_bases(self):
    pytd_cls = self.build_pytd(
        """
      class C: ...
      class D(C): ...
    """,
        'D',
    )
    cls = self.conv.pytd_class_to_value(pytd_cls)
    self.assertEqual(cls.bases, (abstract.SimpleClass(self.ctx, 'C', {}),))

  def test_metaclass(self):
    pytd_cls = self.build_pytd(
        """
      class Meta(type): ...
      class C(metaclass=Meta): ...
    """,
        'C',
    )
    cls = self.conv.pytd_class_to_value(pytd_cls)
    metaclass = cls.metaclass
    self.assertIsNotNone(metaclass)
    self.assertEqual(metaclass.name, 'Meta')


class PytdAliasToValueTest(ConverterTestBase):

  def test_alias(self):
    alias = self.build_pytd('import os.path', name='os.path')
    module = self.conv.pytd_alias_to_value(alias)
    self.assertIsInstance(module, abstract.Module)
    self.assertEqual(module.name, 'os.path')


if __name__ == '__main__':
  unittest.main()
