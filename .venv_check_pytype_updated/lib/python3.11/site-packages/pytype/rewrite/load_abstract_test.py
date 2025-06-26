import numbers

from pytype.rewrite.abstract import abstract
from pytype.rewrite.flow import variables
from pytype.rewrite.tests import test_utils

import unittest


class GetModuleGlobalsTest(test_utils.ContextfulTestBase):

  def test_basic(self):
    module_globals = self.ctx.abstract_loader.get_module_globals()
    # Sanity check a random entry.
    self.assertIn('__name__', module_globals)


class LoadBuiltinTest(test_utils.ContextfulTestBase):

  def test_class(self):
    int_cls = self.ctx.abstract_loader.load_builtin('int')
    self.assertIsInstance(int_cls, abstract.SimpleClass)
    self.assertEqual(int_cls.name, 'int')

  def test_function(self):
    abs_func = self.ctx.abstract_loader.load_builtin('abs')
    self.assertIsInstance(abs_func, abstract.PytdFunction)
    self.assertEqual(abs_func.name, 'abs')

  def test_constant(self):
    ellipsis = self.ctx.abstract_loader.load_builtin('Ellipsis')
    self.assertIsInstance(ellipsis, abstract.PythonConstant)
    self.assertEqual(ellipsis.constant, Ellipsis)

  def test_none(self):
    self.assertIs(
        self.ctx.abstract_loader.load_builtin('None'), self.ctx.consts[None]
    )
    self.assertIs(
        self.ctx.abstract_loader.load_builtin('NoneType'), self.ctx.consts[None]
    )


class LoadValueTest(test_utils.ContextfulTestBase):

  def test_builtin(self):
    self.assertIs(
        self.ctx.abstract_loader.load_value('builtins', 'None'),
        self.ctx.consts[None],
    )

  def test_stdlib(self):
    name = self.ctx.abstract_loader.load_value('os', 'name')
    self.assertIsInstance(name, abstract.FrozenInstance)
    self.assertEqual(name.cls.name, 'str')

  def test_submodule(self):
    submodule = self.ctx.abstract_loader.load_value('os', 'path')
    self.assertIsInstance(submodule, abstract.Module)
    self.assertEqual(submodule.name, 'os.path')


class LoadRawTypeTest(test_utils.ContextfulTestBase):

  def test_builtin_type(self):
    t = self.ctx.abstract_loader.load_raw_type(int)
    self.assertIsInstance(t, abstract.SimpleClass)
    self.assertEqual(t.name, 'int')
    self.assertEqual(t.module, 'builtins')

  def test_stdlib_type(self):
    t = self.ctx.abstract_loader.load_raw_type(numbers.Number)
    self.assertIsInstance(t, abstract.SimpleClass)
    self.assertEqual(t.name, 'Number')
    self.assertEqual(t.module, 'numbers')

  def test_nonetype(self):
    t = self.ctx.abstract_loader.load_raw_type(type(None))
    self.assertIs(t, self.ctx.consts[None])


class LoadTupleTest(test_utils.ContextfulTestBase):

  def assertPythonConstant(self, val, expected):
    self.assertIsInstance(val, abstract.PythonConstant)
    self.assertEqual(val.constant, expected)

  def test_basic(self):
    const = (1, 2, 3)
    t = self.ctx.abstract_loader.build_tuple(const)
    self.assertIsInstance(t, abstract.Tuple)
    self.assertIsInstance(t.constant, tuple)
    self.assertPythonConstant(t.constant[0].values[0], 1)

  def test_nested(self):
    const = (1, (2, 3, 4), 5)
    t = self.ctx.abstract_loader.build_tuple(const)
    self.assertIsInstance(t, abstract.Tuple)
    self.assertIsInstance(t.constant, tuple)
    self.assertIsInstance(t.constant[0], variables.Variable)
    inner = t.constant[1].values[0]
    self.assertIsInstance(inner, abstract.Tuple)
    self.assertIsInstance(inner.constant, tuple)
    self.assertPythonConstant(inner.constant[0].values[0], 2)


if __name__ == '__main__':
  unittest.main()
