"""Tests for blocks.py.

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

from pytype.blocks import blocks
from pytype.blocks import process_blocks
from pytype.directors import annotations
from pytype.pyc import opcodes
from pytype.tests import test_utils

import unittest

o = test_utils.Py310Opcodes


class BaseBlocksTest(unittest.TestCase, test_utils.MakeCodeMixin):
  """A base class for implementing tests testing blocks.py."""

  # These tests check disassembled bytecode, which varies from version to
  # version, so we fix the test version.
  python_version = (3, 10)


class OrderingTest(BaseBlocksTest):
  """Tests for order_code in blocks.py."""

  def _order_code(self, code):
    """Helper function to disassemble and then order code."""
    ordered, _ = blocks.process_code(code)
    return ordered

  def test_trivial(self):
    # Disassembled from:
    # | return None
    co = self.make_code(
        [
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
        ],
        name="trivial",
    )
    ordered_code = self._order_code(co)
    (b0,) = ordered_code.order
    self.assertEqual(len(b0.code), 2)
    self.assertCountEqual([], b0.incoming)
    self.assertCountEqual([], b0.outgoing)

  def test_has_opcode(self):
    # Disassembled from:
    # | return None
    co = self.make_code(
        [
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
        ],
        name="trivial",
    )
    ordered_code = self._order_code(co)
    self.assertTrue(ordered_code.has_opcode(opcodes.LOAD_CONST))
    self.assertTrue(ordered_code.has_opcode(opcodes.RETURN_VALUE))
    self.assertFalse(ordered_code.has_opcode(opcodes.POP_TOP))

  def test_yield(self):
    # Disassembled from:
    # | yield 1
    # | yield None
    co = self.make_code(
        [
            # b0:
            (o.LOAD_CONST, 0),
            (o.YIELD_VALUE, 0),
            # b1:
            (o.POP_TOP, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
        ],
        name="yield",
    )
    ordered_code = self._order_code(co)
    self.assertEqual(ordered_code.name, "yield")
    b0, b1 = ordered_code.order
    self.assertCountEqual(b0.outgoing, [b1])
    self.assertCountEqual(b1.incoming, [b0])
    self.assertCountEqual(b0.incoming, [])
    self.assertCountEqual(b1.outgoing, [])

  def test_triangle(self):
    # Disassembled from:
    # | x = y
    # | if y > 1:
    # |   x -= 2
    # | return x
    co = self.make_code(
        [
            # b0:
            (o.LOAD_GLOBAL, 0),
            (o.STORE_FAST, 0),
            (o.LOAD_GLOBAL, 0),
            (o.LOAD_CONST, 1),
            (o.COMPARE_OP, 4),
            (o.POP_JUMP_IF_FALSE, 10),
            # b1:
            (o.LOAD_FAST, 0),
            (o.LOAD_CONST, 2),
            (o.INPLACE_SUBTRACT, 0),
            (o.STORE_FAST, 0),
            # b2:
            (o.LOAD_FAST, 0),
            (o.RETURN_VALUE, 0),
        ],
        name="triangle",
    )
    ordered_code = self._order_code(co)
    self.assertEqual(ordered_code.name, "triangle")
    b0, b1, b2 = ordered_code.order
    self.assertCountEqual(b0.incoming, [])
    self.assertCountEqual(b0.outgoing, [b1, b2])
    self.assertCountEqual(b1.incoming, [b0])
    self.assertCountEqual(b1.outgoing, [b2])
    self.assertCountEqual(b2.incoming, [b0, b1])
    self.assertCountEqual(b2.outgoing, [])

  def test_diamond(self):
    # Disassembled from:
    # | x = y
    # | if y > 1:
    # |   x -= 2
    # | else:
    # |   x += 2
    # | return x
    co = self.make_code(
        [
            # b0:
            (o.LOAD_GLOBAL, 0),
            (o.STORE_FAST, 0),
            (o.LOAD_GLOBAL, 0),
            (o.LOAD_CONST, 1),
            (o.COMPARE_OP, 4),
            (o.POP_JUMP_IF_FALSE, 12),
            # b1:
            (o.LOAD_FAST, 0),
            (o.LOAD_CONST, 0),
            (o.INPLACE_SUBTRACT, 0),
            (o.STORE_FAST, 0),
            (o.LOAD_FAST, 0),
            (o.RETURN_VALUE, 0),
            # b2:
            (o.LOAD_FAST, 0),
            (o.LOAD_CONST, 0),
            (o.INPLACE_ADD, 0),
            (o.STORE_FAST, 0),
            (o.LOAD_FAST, 0),
            (o.RETURN_VALUE, 0),
        ],
        name="diamond",
    )
    ordered_code = self._order_code(co)
    self.assertEqual(ordered_code.name, "diamond")
    b0, b1, b2 = ordered_code.order
    self.assertCountEqual(b0.incoming, [])
    self.assertCountEqual(b0.outgoing, [b1, b2])
    self.assertCountEqual(b1.incoming, [b0])
    self.assertCountEqual(b2.incoming, [b0])

  def test_raise(self):
    # Disassembled from:
    # | raise ValueError()
    # | return 1
    co = self.make_code(
        [
            # b0:
            (o.LOAD_GLOBAL, 0),
            (o.CALL_FUNCTION, 0),
            (o.RAISE_VARARGS, 1),
            (o.LOAD_CONST, 1),
            (o.RETURN_VALUE, 0),  # dead.
        ],
        name="raise",
    )
    ordered_code = self._order_code(co)
    self.assertEqual(ordered_code.name, "raise")
    b0, b1 = ordered_code.order
    self.assertEqual(len(b0.code), 2)
    self.assertCountEqual(b0.incoming, [])
    self.assertCountEqual(b0.outgoing, [b1])
    self.assertCountEqual(b1.incoming, [b0])
    self.assertCountEqual(b1.outgoing, [])

  def test_call(self):
    # Disassembled from:
    # | f()
    co = self.make_code(
        [
            # b0:
            (o.LOAD_GLOBAL, 0),
            (o.CALL_FUNCTION, 0),
            # b1:
            (o.POP_TOP, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
        ],
        name="call",
    )
    ordered_code = self._order_code(co)
    b0, b1 = ordered_code.order
    self.assertEqual(len(b0.code), 2)
    self.assertEqual(len(b1.code), 3)
    self.assertCountEqual(b0.outgoing, [b1])

  def test_finally(self):
    # Disassembled from:
    # | try:
    # |   pass
    # | finally:
    # |   pass
    co = self.make_code(
        [
            # b0:
            (o.SETUP_FINALLY, 3),
            (o.POP_BLOCK, 0),
            # b1:
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
            # b2:
            (o.RERAISE, 0),
        ],
        name="finally",
    )
    ordered_code = self._order_code(co)
    b0, b1, b2 = ordered_code.order
    self.assertEqual(len(b0.code), 2)
    self.assertEqual(len(b1.code), 2)
    self.assertEqual(len(b2.code), 1)
    self.assertCountEqual(b0.outgoing, [b1, b2])

  def test_except(self):
    # Disassembled from:
    # | try:
    # |   pass
    # | except:
    # |   pass
    co = self.make_code(
        [
            # b0:
            (o.SETUP_FINALLY, 3),
            (o.POP_BLOCK, 0),
            # b1:
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
            # b2:
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
            (o.POP_EXCEPT, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
        ],
        name="except",
    )
    ordered_code = self._order_code(co)
    b0, b1, b2 = ordered_code.order
    self.assertEqual(len(b0.code), 2)
    self.assertEqual(len(b1.code), 2)
    self.assertEqual(len(b2.code), 6)
    self.assertCountEqual([b1, b2], b0.outgoing)

  def test_return(self):
    # Disassembled from:
    # | return None
    # | return None
    co = self.make_code(
        [
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),  # dead.
            (o.LOAD_CONST, 1),  # dead.
            (o.RETURN_VALUE, 0),  # dead.
        ],
        name="return",
    )
    ordered_code = self._order_code(co)
    (b0,) = ordered_code.order
    self.assertEqual(len(b0.code), 2)

  def test_with(self):
    # Disassembled from:
    # | with None:
    # |   pass
    co = self.make_code(
        [
            # b0:
            (o.LOAD_CONST, 0),
            (o.SETUP_WITH, 9),
            (o.POP_TOP, 0),
            (o.POP_BLOCK, 0),
            # b1:
            (o.LOAD_CONST, 0),
            (o.DUP_TOP, 0),
            (o.DUP_TOP, 0),
            (o.CALL_FUNCTION, 3),
            # b2:
            (o.POP_TOP, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
            # b3:
            (o.WITH_EXCEPT_START, 0),
            (o.POP_JUMP_IF_TRUE, 14),
            # b4:
            (o.RERAISE, 1),
            # b5:
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
            (o.POP_EXCEPT, 0),
            (o.POP_TOP, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
        ],
        name="with",
    )
    ordered_code = self._order_code(co)
    b0, b1, b2, b3, b4, b5 = ordered_code.order
    self.assertEqual(len(b0.code), 4)
    self.assertEqual(len(b1.code), 4)
    self.assertEqual(len(b2.code), 3)
    self.assertEqual(len(b3.code), 2)
    self.assertEqual(len(b4.code), 1)
    self.assertEqual(len(b5.code), 7)


class BlockStackTest(BaseBlocksTest):
  """Test the add_pop_block_targets function."""

  def assertTargets(self, code, targets):
    co = self.make_code(code)
    bytecode = opcodes.dis(co)
    blocks.add_pop_block_targets(bytecode)
    for i in range(len(bytecode)):
      op = bytecode[i]
      actual_target = op.target
      actual_block_target = op.block_target
      target_id, block_id = targets.get(i, (None, None))
      expected_target = None if target_id is None else bytecode[target_id]
      expected_block_target = None if block_id is None else bytecode[block_id]
      self.assertEqual(
          actual_target,
          expected_target,
          msg=(
              f"Block {i} ({op!r}) has target {actual_target!r}, "
              f"expected target {expected_target!r}"
          ),
      )
      self.assertEqual(
          actual_block_target,
          expected_block_target,
          msg=(
              f"Block {i} ({op!r}) has block target {actual_block_target!r}, "
              f"expected block target {expected_block_target!r}"
          ),
      )

  def test_finally(self):
    # Disassembled from:
    # | try:
    # |   pass
    # | finally:
    # |   pass
    self.assertTargets(
        [
            (o.SETUP_FINALLY, 3),
            (o.POP_BLOCK, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
            (o.RERAISE, 0),
        ],
        {
            # SETUP_FINALLY.target == RERAISE
            0: (4, None),
            # POP_BLOCK.block_target == RERAISE
            1: (None, 4),
        },
    )

  def test_except(self):
    # Disassembled from:
    # | try:
    # |   pass
    # | except:
    # |   pass
    self.assertTargets(
        [
            (o.SETUP_FINALLY, 3),
            (o.POP_BLOCK, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
            (o.POP_EXCEPT, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
        ],
        {
            # SETUP_FINALLY.target == POP_TOP
            0: (4, None),
            # POP_BLOCK.block_target == POP_TOP
            1: (None, 4),
        },
    )

  def test_with(self):
    # Disassembled from:
    # | with None:
    # |   pass
    self.assertTargets(
        [
            (o.LOAD_CONST, 0),
            (o.SETUP_WITH, 9),
            (o.POP_TOP, 0),
            (o.POP_BLOCK, 0),
            (o.LOAD_CONST, 0),
            (o.DUP_TOP, 0),
            (o.DUP_TOP, 0),
            (o.CALL_FUNCTION, 3),
            (o.POP_TOP, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
            (o.WITH_EXCEPT_START, 0),
            (o.POP_JUMP_IF_TRUE, 14),
            (o.RERAISE, 1),
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
        ],
        {
            # SETUP_WITH.target == WITH_EXCEPT_START
            1: (11, None),
            # POP_BLOCK.block_target == WITH_EXCEPT_START
            3: (None, 11),
            # POP_JUMP_IF_TRUE.target == POP_TOP
            12: (14, None),
        },
    )

  def test_loop(self):
    # Disassembled from:
    # | while []:
    # |   break
    self.assertTargets(
        [
            (o.BUILD_LIST, 0),
            (o.POP_JUMP_IF_FALSE, 4),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
            (o.LOAD_CONST, 0),
            (o.RETURN_VALUE, 0),
        ],
        {
            # POP_JUMP_IF_FALSE.target == LOAD_CONST
            1: (4, None),
        },
    )

  def test_break(self):
    # Disassembled from:
    # | while True:
    # |  if []:
    # |    break
    self.assertTargets(
        [
            (o.NOP, 0),
            (o.BUILD_LIST, 0),
            (o.POP_JUMP_IF_FALSE, 5),
            (o.LOAD_CONST, 1),
            (o.RETURN_VALUE, 0),
            (o.JUMP_ABSOLUTE, 1),
        ],
        {
            # POP_JUMP_IF_FALSE.target == JUMP_ABSOLUTE
            2: (5, None),
            # JUMP_ABSOLUTE.target == BUILD_LIST
            5: (1, None),
        },
    )

  def test_continue(self):
    # Disassembled from:
    # | while True:
    # |   try:
    # |     continue
    # |   except:
    # |     pass
    self.assertTargets(
        [
            (o.NOP, 0),
            (o.SETUP_FINALLY, 2),
            (o.POP_BLOCK, 0),
            (o.JUMP_ABSOLUTE, 0),
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
            (o.POP_TOP, 0),
            (o.POP_EXCEPT, 0),
            (o.JUMP_ABSOLUTE, 1),
        ],
        {
            # SETUP_FINALLY.target == POP_TOP
            1: (4, None),
            # POP_BLOCK.block_target == POP_TOP
            2: (None, 4),
            # JUMP_ABSOLUTE.target == NOP
            3: (0, None),
            # JUMP_ABSOLUTE.target == SETUP_FINALLY
            8: (1, None),
        },
    )

  def test_apply_typecomments(self):
    # Disassembly + type comment map from
    #   a = 1; b = 2  # type: float
    # The type comment should only apply to b.
    co = self.make_code([
        (o.LOAD_CONST, 1),
        (o.STORE_FAST, 0),
        (o.LOAD_CONST, 2),
        (o.STORE_FAST, 1),
        (o.LOAD_CONST, 0),
        (o.RETURN_VALUE, 0),
    ])
    code, _ = blocks.process_code(co)
    ordered_code = process_blocks.merge_annotations(
        code, {1: annotations.VariableAnnotation(None, "float")}, {}
    )
    bytecode = ordered_code.order[0].code
    self.assertIsNone(bytecode[1].annotation)
    self.assertEqual(bytecode[3].annotation, "float")


if __name__ == "__main__":
  unittest.main()
