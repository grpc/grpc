import textwrap
from typing import TypeVar, cast

from pytype.pyc import opcodes
from pytype.rewrite import context
from pytype.rewrite import vm as vm_lib
from pytype.rewrite.abstract import abstract
from pytype.rewrite.tests import test_utils

import unittest

_T = TypeVar('_T')


def _make_vm(src: str) -> vm_lib.VirtualMachine:
  return vm_lib.VirtualMachine.from_source(textwrap.dedent(src))


class VmTest(unittest.TestCase):

  def test_run_module_frame(self):
    block = [
        opcodes.LOAD_CONST(0, 0, 0, 0, 0, 0, None),
        opcodes.RETURN_VALUE(0, 0, 0, 0, 0),
    ]
    code = test_utils.FakeOrderedCode([block], [None])
    vm = vm_lib.VirtualMachine(context.Context(src=''), code.Seal(), {})
    self.assertIsNone(vm._module_frame)
    vm._run_module()
    self.assertIsNotNone(vm._module_frame)

  def test_globals(self):
    vm = _make_vm("""
      x = 42
      def f():
        global y
        y = None
        def g():
          global z
          z = x
        g()
      f()
    """)
    vm._run_module()

    def get_const(val):
      return cast(abstract.PythonConstant, val).constant

    x = get_const(vm._module_frame.final_locals['x'])
    y = get_const(vm._module_frame.final_locals['y'])
    z = get_const(vm._module_frame.final_locals['z'])
    self.assertEqual(x, 42)
    self.assertIsNone(y)
    self.assertEqual(z, 42)

  def test_propagate_nonlocal(self):
    vm = _make_vm("""
      def f():
        x = None
        def g():
          def h():
            nonlocal x
            x = 5
          h()
        g()
        global y
        y = x
      f()
    """)
    vm._run_module()
    with self.assertRaises(KeyError):
      _ = vm._module_frame.final_locals['x']
    y = cast(abstract.PythonConstant, vm._module_frame.final_locals['y'])
    self.assertEqual(y.constant, 5)


if __name__ == '__main__':
  unittest.main()
