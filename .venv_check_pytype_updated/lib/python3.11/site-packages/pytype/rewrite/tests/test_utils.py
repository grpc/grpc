"""Test utilities."""

from collections.abc import Sequence
import re
import sys
import textwrap

from pytype.blocks import blocks
from pytype.pyc import opcodes
from pytype.pyc import pyc
from pytype.pytd.parse import parser_test_base
from pytype.rewrite import context
from pytype_extensions import instrumentation_for_testing as i4t

import unittest


class ContextfulTestBase(unittest.TestCase):

  def setUp(self):
    super().setUp()
    self.ctx = context.Context(src='')


class PytdTestBase(parser_test_base.ParserTest):
  """Base for tests that build pytd objects."""

  def build_pytd(self, src, name=None):
    pytd_tree = self.ParseWithBuiltins(src)
    if name:
      member = pytd_tree.Lookup(name)
    else:
      # Ignore aliases because they may be imports.
      members = (
          pytd_tree.constants
          + pytd_tree.type_params
          + pytd_tree.classes
          + pytd_tree.functions
      )
      assert len(members) == 1
      (member,) = members
      name = member.name
    return member.Replace(name=f'<test>.{name}')


class FakeOrderedCode(i4t.ProductionType[blocks.OrderedCode]):

  def __init__(self, ops: Sequence[list[opcodes.Opcode]], consts=()):
    self.order = [blocks.Block(block_ops) for block_ops in ops]
    self.consts = consts


# pylint: disable=invalid-name
# Use camel-case to match the unittest.skip* methods.
def skipIfPy(*versions, reason):
  return unittest.skipIf(sys.version_info[:2] in versions, reason)


def skipUnlessPy(*versions, reason):
  return unittest.skipUnless(sys.version_info[:2] in versions, reason)


def skipBeforePy(version, reason):
  return unittest.skipIf(sys.version_info[:2] < version, reason)


def skipFromPy(version, reason):
  return unittest.skipUnless(sys.version_info[:2] < version, reason)


def skipOnWin32(reason):
  return unittest.skipIf(sys.platform == 'win32', reason)


# pylint: enable=invalid-name


def parse(src: str) -> blocks.OrderedCode:
  code = pyc.compile_src(
      src=textwrap.dedent(src),
      python_version=sys.version_info[:2],
      python_exe=None,
      filename='<inline>',
      mode='exec',
  )
  ordered_code, unused_block_graph = blocks.process_code(code)
  return ordered_code


def assemble_block(bytecode: str, *, consts=()) -> FakeOrderedCode:
  """Generate a block of opcodes for tests.

  Args:
    bytecode: A block of opcodes
    consts: A sequence of constants (co_consts in the compiled code)

  Returns:
    A FakeOrderedCode

  The bytecode is a block of text, one opcode per line, in the format
    # line <lineno>
    OP_WITH_ARG arg  # comment
    OP  # comment
    ...

  Blank lines are ignored.

  The line numbers are optional, all opcodes will get lineno=1 if omitted. If a
  line number is supplied, all following opcodes get that line number until
  another line number is encountered.
  """

  lines = textwrap.dedent(bytecode).split('\n')
  ret = []
  idx, lineno = 0, 1
  for line in lines:
    if m := re.match(r'# line (\d+)', line.strip()):
      lineno = int(m.group(1))
      continue
    line = re.sub(r'#.*$', '', line)  # allow comments
    parts = line.split()
    if not parts:
      continue
    if len(parts) == 1:
      (op,) = parts
      arg = None
    else:
      op, arg, *extra = parts
      assert not extra, extra
      arg = int(arg)
    op_cls = getattr(opcodes, op)
    if arg is not None:
      ret.append(op_cls(idx, lineno, lineno, 0, 0, arg, None))
    else:
      ret.append(op_cls(idx, lineno))
    idx += 1
  return FakeOrderedCode([ret], consts)
