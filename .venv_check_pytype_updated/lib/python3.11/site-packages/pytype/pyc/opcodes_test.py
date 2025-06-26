import itertools
import pycnite.types
from pytype.pyc import opcodes
import unittest


class _TestBase(unittest.TestCase):
  """Base class for all opcodes.dis testing."""

  def dis(self, data, **kwargs):
    """Return the opcodes from disassembling a code sequence."""
    defaults = {
        'co_code': data,
        'co_argcount': 0,
        'co_posonlyargcount': 0,
        'co_kwonlyargcount': 0,
        'co_nlocals': 0,
        'co_stacksize': 0,
        'co_flags': 0,
        'co_consts': [],
        'co_names': [],
        'co_varnames': [],
        'co_filename': '',
        'co_name': '',
        'co_firstlineno': 0,
        'co_lnotab': [],
        'co_freevars': [],
        'co_cellvars': [],
        'python_version': self.python_version,
    }
    defaults.update(kwargs)
    code = pycnite.types.CodeType38(**defaults)
    return opcodes.dis(code)

  def assertSimple(self, opcode, name):
    """Assert that a single opcode byte disassembles to the given name."""
    self.assertName([opcode], name)

  def assertName(self, code, name):
    """Assert that the first disassembled opcode has the given name."""
    self.assertEqual(self.dis(code)[0].name, name)

  def assertDisassembly(self, code, expected):
    """Assert that an extended code sequence has the expected disassembly."""
    ops = self.dis(list(itertools.chain(*code)))
    self.assertEqual(len(ops), len(expected))
    for o, e in zip(ops, expected):
      if len(e) == 1:
        self.assertEqual(e, (o.name,))
      else:
        self.assertEqual(e, (o.name, o.arg))

  def assertLineNumbers(self, code, co_lnotab, expected):
    """Assert that the opcodes have the expected line numbers."""
    ops = self.dis(
        list(itertools.chain(*code)),
        co_lnotab=bytes(list(itertools.chain(*co_lnotab))),
        co_firstlineno=1,
    )
    self.assertEqual(len(ops), len(expected))
    for o, e in zip(ops, expected):
      self.assertEqual(e, o.line)


class CommonTest(_TestBase):
  """Test bytecodes that are common to multiple Python versions."""

  python_version = (3, 10)

  def test_pop_top(self):
    self.assertSimple(1, 'POP_TOP')

  def test_store_name(self):
    self.assertName([90, 0], 'STORE_NAME')

  def test_for_iter(self):
    self.assertName([93, 0, 9], 'FOR_ITER')

  def test_extended_disassembly(self):
    code = [
        (0x7C, 0),  # 0 LOAD_FAST, arg=0,
        (0x7C, 0),  # 3 LOAD_FAST, arg=0,
        (0x17,),  # 6 BINARY_ADD,
        (0x01,),  # 7 POP_TOP,
        (0x7C, 0),  # 8 LOAD_FAST, arg=0,
        (0x7C, 0),  # 11 LOAD_FAST, arg=0,
        (0x14,),  # 14 BINARY_MULTIPLY,
        (0x01,),  # 15 POP_TOP,
        (0x7C, 0),  # 16 LOAD_FAST, arg=0,
        (0x7C, 0),  # 19 LOAD_FAST, arg=0,
        (0x16,),  # 22 BINARY_MODULO,
        (0x01,),  # 23 POP_TOP,
        (0x7C, 0),  # 24 LOAD_FAST, arg=0,
        (0x7C, 0),  # 27 LOAD_FAST, arg=0,
        (0x1B,),  # 30 BINARY_TRUE_DIVIDE,
        (0x01,),  # 31 POP_TOP,
        (0x64, 0),  # 32 LOAD_CONST, arg=0,
        (0x53, 0),  # 35 RETURN_VALUE
    ]
    # The POP_TOP instructions are discarded.
    expected = [
        ('LOAD_FAST', 0),
        ('LOAD_FAST', 0),
        ('BINARY_ADD',),
        ('LOAD_FAST', 0),
        ('LOAD_FAST', 0),
        ('BINARY_MULTIPLY',),
        ('LOAD_FAST', 0),
        ('LOAD_FAST', 0),
        ('BINARY_MODULO',),
        ('LOAD_FAST', 0),
        ('LOAD_FAST', 0),
        ('BINARY_TRUE_DIVIDE',),
        ('LOAD_CONST', 0),
        ('RETURN_VALUE',),
    ]
    self.assertDisassembly(code, expected)


class Python38Test(_TestBase):
  python_version = (3, 8, 0)

  def test_non_monotonic_line_numbers(self):
    # Make sure we can deal with line number tables that aren't
    # monotonic. That is:
    #
    # line 1: OPCODE_1
    # line 2: OPCODE_2
    # line 1: OPCODE 3

    # Compiled from:
    # f(
    #   1,
    #   2
    #  )
    code = [
        (0x65, 0),  # LOAD_NAME, arg=0th name
        (0x64, 0),  # LOAD_CONST, arg=0th constant
        (0x64, 1),  # LOAD_CONST, arg=1st constant
        (0x83, 0x2),  # CALL_FUNCTION, arg=2 function arguments
        (0x53, 0x0),  # RETURN_VALUE
    ]
    expected = [
        ('LOAD_NAME', 0),
        ('LOAD_CONST', 0),
        ('LOAD_CONST', 1),
        ('CALL_FUNCTION', 2),
        ('RETURN_VALUE',),
    ]
    self.assertDisassembly(code, expected)
    lnotab = [
        (0x2, 0x1),  # +2 addr, +1 line number
        (0x2, 0x1),  # +2 addr, +1 line number
        (0x2, 0xFE),  # +2 addr, -2 line number
    ]
    self.assertLineNumbers(code, lnotab, [1, 2, 3, 1, 1])


class ExceptionBitmaskTest(unittest.TestCase):
  """Tests for opcodes._get_exception_bitmask."""

  def assertBitmask(self, *, offset_to_op, exc_ranges, expected_bitmask):
    bitmask = bin(opcodes._get_exception_bitmask(offset_to_op, exc_ranges))
    self.assertEqual(bitmask, expected_bitmask)

  def test_one_exception_range(self):
    self.assertBitmask(
        offset_to_op={1: None, 5: None, 8: None, 13: None},
        exc_ranges={4: 10},
        expected_bitmask='0b11111110000',
    )

  def test_multiple_exception_ranges(self):
    self.assertBitmask(
        offset_to_op={1: None, 3: None, 5: None, 7: None, 9: None},
        exc_ranges={1: 4, 7: 9},
        expected_bitmask='0b1110011110',
    )

  def test_length_one_range(self):
    self.assertBitmask(
        offset_to_op={0: None, 3: None, 6: None, 7: None, 12: None},
        exc_ranges={0: 0, 6: 6, 7: 7, 12: 12},
        expected_bitmask='0b1000011000001',
    )

  def test_overlapping_ranges(self):
    self.assertBitmask(
        offset_to_op={1: None, 5: None, 8: None, 13: None},
        exc_ranges={1: 5, 4: 9},
        expected_bitmask='0b1111111110',
    )

  def test_no_exception(self):
    self.assertBitmask(
        offset_to_op={1: None, 5: None, 8: None, 13: None},
        exc_ranges={},
        expected_bitmask='0b0',
    )


if __name__ == '__main__':
  unittest.main()
