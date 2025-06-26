"""Tests for vm.py.

To create test cases, you can disassemble source code with the help of the dis
module. For example, in Python 3.7, this snippet:

  import dis
  import opcode
  def f(): return None
  bytecode = dis.Bytecode(f)
  for x in bytecode.codeobj.co_code:
    print(f'{x} ({opcode.opname[x]})')

prints:

  100 (LOAD_CONST)
  0 (<0>)
  83 (RETURN_VALUE)
  0 (<0>)
"""

import textwrap

from pytype import context
from pytype import vm
from pytype.tests import test_base
from pytype.tests import test_utils


class TraceVM(vm.VirtualMachine):
  """Special VM that remembers which instructions it executed."""

  def __init__(self, ctx):
    super().__init__(ctx)
    # There are multiple possible orderings of the basic blocks of the code, so
    # we collect the instructions in an order-independent way:
    self.instructions_executed = set()
    # Extra stuff that's defined in tracer_vm.CallTracer:
    self._call_trace = set()
    self._functions = set()
    self._classes = set()
    self._unknowns = []

  def run_instruction(self, op, state):
    self.instructions_executed.add(op.index)
    return super().run_instruction(op, state)


class VmTestBase(test_base.BaseTest, test_utils.MakeCodeMixin):
  """Base for VM tests."""

  def setUp(self):
    super().setUp()
    self.ctx = self.make_context()

  def make_context(self):
    return context.Context(options=self.options, loader=self.loader, src="")


class TraceVmTestBase(VmTestBase):
  """Base for VM tests with a tracer vm."""

  def setUp(self):
    super().setUp()
    self.ctx.vm = TraceVM(self.ctx)


class TraceTest(TraceVmTestBase):
  """Tests for opcode tracing in the VM."""

  def test_empty_data(self):
    """Test that we can trace values without data."""
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    self.ctx.vm.trace_opcode(op, "x", 42)
    self.assertEqual(self.ctx.vm.opcode_traces, [(op, "x", (None,))])

  def test_const(self):
    src = textwrap.dedent("""
      x = 1  # line 1
      y = x  # line 2
    """).lstrip()
    if self.ctx.python_version >= (3, 12):
      # Compiles to:
      #     2 LOAD_CONST     0 (1)
      #     4 STORE_NAME     0 (x)
      #
      #     6 LOAD_NAME      0 (x)
      #     8 STORE_NAME     1 (y)
      #    10 RETURN_CONST   1 (None)
      expected = [
          # (opcode, line number, symbol)
          ("LOAD_CONST", 1, 1),
          ("STORE_NAME", 1, "x"),
          ("LOAD_NAME", 2, "x"),
          ("STORE_NAME", 2, "y"),
          ("RETURN_CONST", 2, None),
      ]
    else:
      # Compiles to:
      #     0 LOAD_CONST     0 (1)
      #     3 STORE_NAME     0 (x)
      #
      #     6 LOAD_NAME      0 (x)
      #     9 STORE_NAME     1 (y)
      #    12 LOAD_CONST     1 (None)
      #    15 RETURN_VALUE
      expected = [
          # (opcode, line number, symbol)
          ("LOAD_CONST", 1, 1),
          ("STORE_NAME", 1, "x"),
          ("LOAD_NAME", 2, "x"),
          ("STORE_NAME", 2, "y"),
          ("LOAD_CONST", 2, None),
      ]
    self.ctx.vm.run_program(src, "", maximum_depth=10)
    actual = [
        (op.name, op.line, symbol)
        for op, symbol, _ in self.ctx.vm.opcode_traces
    ]
    self.assertEqual(actual, expected)


class AnnotationsTest(VmTestBase):
  """Tests for recording annotations."""

  def test_record_local_ops(self):
    self.ctx.vm.run_program("v: int = None", "", maximum_depth=10)
    self.assertEqual(
        self.ctx.vm.local_ops,
        {
            "<module>": [
                vm.LocalOp(name="v", op=vm.LocalOp.Op.ASSIGN),
                vm.LocalOp(name="v", op=vm.LocalOp.Op.ANNOTATE),
            ]
        },
    )


if __name__ == "__main__":
  test_base.main()
