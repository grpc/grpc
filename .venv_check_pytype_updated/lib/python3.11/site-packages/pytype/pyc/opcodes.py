"""Opcode definitions."""

from typing import cast

import attrs
from pycnite import bytecode
import pycnite.types
from typing_extensions import override


# We define all-uppercase classes, to match their opcode names:
# pylint: disable=invalid-name

HAS_CONST = 1  # references the constant table
HAS_NAME = 2  # references the name table
HAS_JREL = 4  # relative jump
HAS_JABS = 8  # absolute jump
HAS_JUNKNOWN = 16  # jumps to unknown location
HAS_LOCAL = 32  # references the varnames table
HAS_FREE = 64  # references "free variable" cells
HAS_NARGS = 128  # stores number of args + kwargs
HAS_ARGUMENT = 256  # all opcodes >= 90
NO_NEXT = 512  # doesn't execute the following opcode
STORE_JUMP = 1024  # only stores a jump, doesn't actually execute it
PUSHES_BLOCK = 2048  # starts a block (while, try, finally, with, etc.)
POPS_BLOCK = 4096  # ends a block


@attrs.define(slots=True)
class OpcodeMetadata:
  """Contextual metadata attached to opcodes."""

  # Function signature annotations in textual form
  signature_annotations: dict[str, str] | None = None
  # Code run out of line-number order, due to compiler optimisations.
  is_out_of_order: bool = False


class Opcode:
  """An opcode without arguments."""

  __slots__ = (
      "line",
      "endline",
      "col",
      "endcol",
      "index",
      "prev",
      "next",
      "target",
      "block_target",
      "code",
      "annotation",
      "folded",
      "metadata",
      "push_exc_block",
      "pop_exc_block",
  )
  _FLAGS = 0

  def __init__(self, index, line, endline=None, col=None, endcol=None):
    self.index = index
    self.line = line
    self.endline = endline
    self.col = col
    self.endcol = endcol
    self.prev = None
    self.next = None
    self.target = None
    self.block_target = None
    self.code = None  # If we have a CodeType or OrderedCode parent
    self.annotation = None
    self.folded = None  # elided by constant folding
    self.metadata = OpcodeMetadata()  # Filled in by the director
    self.push_exc_block = False
    self.pop_exc_block = False

  def at_line(self, line):
    """Return a new opcode similar to this one but with a different line."""
    # Ignore the optional slots (prev, next, block_target).
    op = Opcode(self.index, line)
    op.target = self.target
    op.code = self.code
    return op

  def basic_str(self):
    """Helper function for the various __str__ formats."""
    folded = "<<<<" if self.folded else ""
    return "%d: %d: %s %s" % (
        self.line,
        self.index,
        self.__class__.__name__,
        folded,
    )

  def __str__(self):
    if self.annotation:
      return f"{self.basic_str()}  # type: {self.annotation}"
    else:
      return self.basic_str()

  def __repr__(self):
    return self.__class__.__name__

  @property
  def name(self):
    return self.__class__.__name__

  @classmethod
  def for_python_version(
      cls, version: tuple[int, int]  # pylint: disable=unused-argument
  ):
    return cls

  @classmethod
  def has_const(cls):
    return bool(cls._FLAGS & HAS_CONST)

  @classmethod
  def has_name(cls):
    return bool(cls._FLAGS & HAS_NAME)

  @classmethod
  def has_jrel(cls):
    return bool(cls._FLAGS & HAS_JREL)

  @classmethod
  def has_jabs(cls):
    return bool(cls._FLAGS & HAS_JABS)

  @classmethod
  def has_known_jump(cls):
    return bool(cls._FLAGS & (HAS_JREL | HAS_JABS))

  @classmethod
  def has_junknown(cls):
    return bool(cls._FLAGS & HAS_JUNKNOWN)

  @classmethod
  def has_jump(cls):
    return bool(cls._FLAGS & (HAS_JREL | HAS_JABS | HAS_JUNKNOWN))

  @classmethod
  def has_local(cls):
    return bool(cls._FLAGS & HAS_LOCAL)

  @classmethod
  def has_free(cls):
    return bool(cls._FLAGS & HAS_FREE)

  @classmethod
  def has_nargs(cls):
    return bool(cls._FLAGS & HAS_NARGS)

  @classmethod
  def has_argument(cls):
    return bool(cls._FLAGS & HAS_ARGUMENT)

  @classmethod
  def no_next(cls):
    return bool(cls._FLAGS & NO_NEXT)

  @classmethod
  def carry_on_to_next(cls):
    return not cls._FLAGS & NO_NEXT

  @classmethod
  def store_jump(cls):
    return bool(cls._FLAGS & STORE_JUMP)

  @classmethod
  def does_jump(cls):
    return cls.has_jump() and not cls.store_jump()

  @classmethod
  def pushes_block(cls):
    return bool(cls._FLAGS & PUSHES_BLOCK)

  @classmethod
  def pops_block(cls):
    return bool(cls._FLAGS & POPS_BLOCK)


class OpcodeWithArg(Opcode):
  """An opcode with one argument.

  Attributes:
    arg: The integer opcode argument read in from the bytecode
    argval: A decoded version of arg, performing the same steps the cpython
      interpreter does to convert arg into a python value.
  """

  __slots__ = ("arg", "argval")

  def __init__(self, index, line, endline, col, endcol, arg, argval):
    super().__init__(index, line, endline, col, endcol)
    self.arg = arg
    self.argval = argval

  def __str__(self):
    out = f"{self.basic_str()} {self.argval}"
    if self.annotation:
      return f"{out}  # type: {self.annotation}"
    else:
      return out


# --------------------------------------------------------
# Fake opcodes used internally


class LOAD_FOLDED_CONST(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()

  def __str__(self):
    return self.basic_str() + " " + str(self.arg.value)


class SETUP_EXCEPT_311(OpcodeWithArg):
  _FLAGS = HAS_JREL | HAS_ARGUMENT | STORE_JUMP | PUSHES_BLOCK
  __slots__ = ("stack_depth",)


# --------------------------------------------------------


class POP_TOP(Opcode):
  __slots__ = ()


class ROT_TWO(Opcode):
  __slots__ = ()


class ROT_THREE(Opcode):
  __slots__ = ()


class DUP_TOP(Opcode):
  __slots__ = ()


class ROT_FOUR(Opcode):
  __slots__ = ()


class DUP_TOP_TWO(Opcode):
  __slots__ = ()


class NOP(Opcode):
  __slots__ = ()


class UNARY_POSITIVE(Opcode):
  __slots__ = ()


class UNARY_NEGATIVE(Opcode):
  __slots__ = ()


class UNARY_NOT(Opcode):
  __slots__ = ()


class UNARY_INVERT(Opcode):
  __slots__ = ()


class BINARY_MATRIX_MULTIPLY(Opcode):
  __slots__ = ()


class INPLACE_MATRIX_MULTIPLY(Opcode):
  __slots__ = ()


class BINARY_POWER(Opcode):
  __slots__ = ()


class BINARY_MULTIPLY(Opcode):
  __slots__ = ()


class BINARY_MODULO(Opcode):
  __slots__ = ()


class BINARY_ADD(Opcode):
  __slots__ = ()


class BINARY_SUBTRACT(Opcode):
  __slots__ = ()


class BINARY_SUBSCR(Opcode):
  __slots__ = ()


class BINARY_FLOOR_DIVIDE(Opcode):
  __slots__ = ()


class BINARY_TRUE_DIVIDE(Opcode):
  __slots__ = ()


class INPLACE_FLOOR_DIVIDE(Opcode):
  __slots__ = ()


class INPLACE_TRUE_DIVIDE(Opcode):
  __slots__ = ()


class GET_AITER(Opcode):
  __slots__ = ()


class GET_ANEXT(Opcode):
  __slots__ = ()


class BEFORE_ASYNC_WITH(Opcode):
  __slots__ = ()


class BEGIN_FINALLY(Opcode):
  __slots__ = ()


class END_ASYNC_FOR(Opcode):
  # Even though dis documentation says that END_ASYNC_FOR may reraise an
  # exception, we do not include NO_NEXT in the flags because doing so would
  # cause the return statement for an async method to be skipped, leading to
  # an incorrect return type.
  # See tests/test_stdlib2:StdlibTestsFeatures.test_async_iter and
  # tests/test_coroutine:GeneratorFeatureTest.test_async_for_pyi for tests
  # that fail if we add NO_NEXT.
  _FLAGS = HAS_JUNKNOWN
  __slots__ = ()


class INPLACE_ADD(Opcode):
  __slots__ = ()


class INPLACE_SUBTRACT(Opcode):
  __slots__ = ()


class INPLACE_MULTIPLY(Opcode):
  __slots__ = ()


class INPLACE_MODULO(Opcode):
  __slots__ = ()


class STORE_SUBSCR(Opcode):
  __slots__ = ()


class DELETE_SUBSCR(Opcode):
  __slots__ = ()


class BINARY_LSHIFT(Opcode):
  __slots__ = ()


class BINARY_RSHIFT(Opcode):
  __slots__ = ()


class BINARY_AND(Opcode):
  __slots__ = ()


class BINARY_XOR(Opcode):
  __slots__ = ()


class BINARY_OR(Opcode):
  __slots__ = ()


class INPLACE_POWER(Opcode):
  __slots__ = ()


class GET_ITER(Opcode):
  __slots__ = ()


class GET_YIELD_FROM_ITER(Opcode):
  __slots__ = ()


class PRINT_EXPR(Opcode):
  __slots__ = ()


class LOAD_BUILD_CLASS(Opcode):
  __slots__ = ()


class YIELD_FROM(Opcode):
  _FLAGS = HAS_JUNKNOWN
  __slots__ = ()


# NOTE: GET_AWAITABLE gains an argument in Python 3.11, but adding the argument
# here causes tests in earlier versions to fail.
class GET_AWAITABLE(Opcode):
  __slots__ = ()


class INPLACE_LSHIFT(Opcode):
  __slots__ = ()


class INPLACE_RSHIFT(Opcode):
  __slots__ = ()


class INPLACE_AND(Opcode):
  __slots__ = ()


class INPLACE_XOR(Opcode):
  __slots__ = ()


class INPLACE_OR(Opcode):
  __slots__ = ()


class BREAK_LOOP(Opcode):
  _FLAGS = HAS_JUNKNOWN | NO_NEXT
  __slots__ = ()


class WITH_CLEANUP_START(Opcode):
  _FLAGS = HAS_JUNKNOWN  # might call __exit__
  __slots__ = ()


class WITH_CLEANUP_FINISH(Opcode):
  __slots__ = ()


class RETURN_VALUE(Opcode):
  _FLAGS = HAS_JUNKNOWN | NO_NEXT
  __slots__ = ()


class IMPORT_STAR(Opcode):
  __slots__ = ()


class SETUP_ANNOTATIONS(Opcode):
  __slots__ = ()


class YIELD_VALUE(OpcodeWithArg):
  """YIELD_VALUE opcode, for different Python versions."""

  _FLAGS = HAS_ARGUMENT
  __slots__ = ()

  @classmethod
  @override
  def for_python_version(cls, version: tuple[int, int]):
    if version <= (3, 11):

      # Intentionally use the same class name, so that __class__.__name__ stays
      # the same, which is used in several parts of the code.
      class YIELD_VALUE(Opcode):  # pylint: disable=redefined-outer-name
        _FLAGS = HAS_JUNKNOWN
        __slots__ = ()

      return YIELD_VALUE
    return cls


class POP_BLOCK(Opcode):
  _FLAGS = POPS_BLOCK
  __slots__ = ()


class END_FINALLY(Opcode):
  _FLAGS = HAS_JUNKNOWN  # might re-raise an exception
  __slots__ = ()


class POP_EXCEPT(Opcode):
  __slots__ = ()


class STORE_NAME(OpcodeWithArg):  # Indexes into name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class DELETE_NAME(OpcodeWithArg):  # Indexes into name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class UNPACK_SEQUENCE(OpcodeWithArg):  # Arg: Number of tuple items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class FOR_ITER(OpcodeWithArg):
  _FLAGS = HAS_JREL | HAS_ARGUMENT
  __slots__ = ()


class LIST_APPEND(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class UNPACK_EX(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class STORE_ATTR(OpcodeWithArg):  # Indexes into name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class DELETE_ATTR(OpcodeWithArg):  # Indexes into name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class STORE_GLOBAL(OpcodeWithArg):  # Indexes into name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class DELETE_GLOBAL(OpcodeWithArg):  # Indexes into name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class LOAD_CONST(OpcodeWithArg):  # Arg: Index in const list
  _FLAGS = HAS_ARGUMENT | HAS_CONST
  __slots__ = ()


class LOAD_NAME(OpcodeWithArg):  # Arg: Index in name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class BUILD_TUPLE(OpcodeWithArg):  # Arg: Number of tuple items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_LIST(OpcodeWithArg):  # Arg: Number of list items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_SET(OpcodeWithArg):  # Arg: Number of set items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_MAP(OpcodeWithArg):  # Arg: Number of dict entries (up to 255)
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class LOAD_ATTR(OpcodeWithArg):  # Arg: Index in name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class COMPARE_OP(OpcodeWithArg):  # Arg: Comparison operator
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class IMPORT_NAME(OpcodeWithArg):  # Arg: Index in name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT | HAS_JUNKNOWN
  __slots__ = ()


class IMPORT_FROM(OpcodeWithArg):  # Arg: Index in name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class JUMP_FORWARD(OpcodeWithArg):
  _FLAGS = HAS_JREL | HAS_ARGUMENT | NO_NEXT
  __slots__ = ()


class JUMP_IF_FALSE_OR_POP(OpcodeWithArg):
  _FLAGS = HAS_JABS | HAS_ARGUMENT
  __slots__ = ()


class JUMP_IF_TRUE_OR_POP(OpcodeWithArg):
  _FLAGS = HAS_JABS | HAS_ARGUMENT
  __slots__ = ()


class JUMP_ABSOLUTE(OpcodeWithArg):
  _FLAGS = HAS_JABS | HAS_ARGUMENT | NO_NEXT
  __slots__ = ()


class POP_JUMP_IF_FALSE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class POP_JUMP_IF_TRUE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class LOAD_GLOBAL(OpcodeWithArg):  # Indexes into name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class CONTINUE_LOOP(OpcodeWithArg):  # Acts as jump
  _FLAGS = HAS_JABS | HAS_ARGUMENT | NO_NEXT
  __slots__ = ()


class SETUP_LOOP(OpcodeWithArg):
  _FLAGS = HAS_JREL | HAS_ARGUMENT | STORE_JUMP | PUSHES_BLOCK
  __slots__ = ()


class SETUP_EXCEPT(OpcodeWithArg):
  _FLAGS = HAS_JREL | HAS_ARGUMENT | STORE_JUMP | PUSHES_BLOCK
  __slots__ = ()


class SETUP_FINALLY(OpcodeWithArg):
  _FLAGS = HAS_JREL | HAS_ARGUMENT | STORE_JUMP | PUSHES_BLOCK
  __slots__ = ()


class LOAD_FAST(OpcodeWithArg):  # Loads local variable number
  _FLAGS = HAS_LOCAL | HAS_ARGUMENT
  __slots__ = ()


class STORE_FAST(OpcodeWithArg):  # Stores local variable number
  _FLAGS = HAS_LOCAL | HAS_ARGUMENT
  __slots__ = ()


class DELETE_FAST(OpcodeWithArg):  # Deletes local variable number
  _FLAGS = HAS_LOCAL | HAS_ARGUMENT
  __slots__ = ()


class STORE_ANNOTATION(OpcodeWithArg):
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class RAISE_VARARGS(OpcodeWithArg):  # Arg: Number of raise args (1, 2, or 3)
  _FLAGS = HAS_ARGUMENT | HAS_JUNKNOWN | NO_NEXT
  __slots__ = ()


class CALL_FUNCTION(OpcodeWithArg):  # Arg: #args + (#kwargs << 8)
  _FLAGS = HAS_NARGS | HAS_ARGUMENT | HAS_JUNKNOWN
  __slots__ = ()


class MAKE_FUNCTION(OpcodeWithArg):  # Arg: Number of args with default values
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_SLICE(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class MAKE_CLOSURE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class LOAD_CLOSURE(OpcodeWithArg):
  _FLAGS = HAS_FREE | HAS_ARGUMENT
  __slots__ = ()


class LOAD_DEREF(OpcodeWithArg):
  _FLAGS = HAS_FREE | HAS_ARGUMENT
  __slots__ = ()


class STORE_DEREF(OpcodeWithArg):
  _FLAGS = HAS_FREE | HAS_ARGUMENT
  __slots__ = ()


class DELETE_DEREF(OpcodeWithArg):
  _FLAGS = HAS_FREE | HAS_ARGUMENT
  __slots__ = ()


class CALL_FUNCTION_VAR(OpcodeWithArg):  # Arg: #args + (#kwargs << 8)
  _FLAGS = HAS_NARGS | HAS_ARGUMENT | HAS_JUNKNOWN
  __slots__ = ()


class CALL_FUNCTION_KW(OpcodeWithArg):  # Arg: #args + (#kwargs << 8)
  _FLAGS = HAS_NARGS | HAS_ARGUMENT | HAS_JUNKNOWN
  __slots__ = ()


class CALL_FUNCTION_VAR_KW(OpcodeWithArg):  # Arg: #args + (#kwargs << 8)
  _FLAGS = HAS_NARGS | HAS_ARGUMENT | HAS_JUNKNOWN
  __slots__ = ()


class CALL_FUNCTION_EX(OpcodeWithArg):  # Arg: flags
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class SETUP_WITH(OpcodeWithArg):
  _FLAGS = HAS_JREL | HAS_ARGUMENT | STORE_JUMP | PUSHES_BLOCK
  __slots__ = ()


class EXTENDED_ARG(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class SET_ADD(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class MAP_ADD(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class LOAD_CLASSDEREF(OpcodeWithArg):
  _FLAGS = HAS_FREE | HAS_ARGUMENT
  __slots__ = ()


class BUILD_LIST_UNPACK(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_MAP_UNPACK(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_MAP_UNPACK_WITH_CALL(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_TUPLE_UNPACK(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_SET_UNPACK(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class SETUP_ASYNC_WITH(OpcodeWithArg):
  _FLAGS = HAS_JREL | HAS_ARGUMENT | STORE_JUMP | PUSHES_BLOCK
  __slots__ = ()


class FORMAT_VALUE(OpcodeWithArg):  # Arg: Flags
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_CONST_KEY_MAP(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_STRING(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BUILD_TUPLE_UNPACK_WITH_CALL(OpcodeWithArg):  # Arg: Number of items
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class LOAD_METHOD(OpcodeWithArg):  # Arg: Index in name list
  _FLAGS = HAS_NAME | HAS_ARGUMENT
  __slots__ = ()


class CALL_METHOD(OpcodeWithArg):  # Arg: #args
  _FLAGS = HAS_NARGS | HAS_ARGUMENT | HAS_JUNKNOWN
  __slots__ = ()


class CALL_FINALLY(OpcodeWithArg):  # Arg: Jump offset to finally block
  _FLAGS = HAS_JREL | HAS_ARGUMENT
  __slots__ = ()


class POP_FINALLY(OpcodeWithArg):
  # might re-raise an exception or jump to a finally
  _FLAGS = HAS_ARGUMENT | HAS_JUNKNOWN
  __slots__ = ()


class WITH_EXCEPT_START(Opcode):
  __slots__ = ()


class LOAD_ASSERTION_ERROR(Opcode):
  __slots__ = ()


class LIST_TO_TUPLE(Opcode):
  __slots__ = ()


class IS_OP(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class CONTAINS_OP(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class JUMP_IF_NOT_EXC_MATCH(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JABS
  __slots__ = ()


class LIST_EXTEND(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class SET_UPDATE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class DICT_MERGE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class DICT_UPDATE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class GET_LEN(Opcode):
  __slots__ = ()


class MATCH_MAPPING(Opcode):
  __slots__ = ()


class MATCH_SEQUENCE(Opcode):
  __slots__ = ()


class MATCH_KEYS(Opcode):
  __slots__ = ()


class COPY_DICT_WITHOUT_KEYS(Opcode):
  __slots__ = ()


class ROT_N(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class RERAISE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | NO_NEXT
  __slots__ = ()


class GEN_START(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class MATCH_CLASS(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class CACHE(Opcode):
  __slots__ = ()


class PUSH_NULL(Opcode):
  __slots__ = ()


class PUSH_EXC_INFO(Opcode):
  __slots__ = ()


class CHECK_EXC_MATCH(Opcode):
  __slots__ = ()


class CHECK_EG_MATCH(Opcode):
  __slots__ = ()


class BEFORE_WITH(Opcode):
  __slots__ = ()


class RETURN_GENERATOR(Opcode):
  __slots__ = ()


class ASYNC_GEN_WRAP(Opcode):
  __slots__ = ()


class PREP_RERAISE_STAR(Opcode):
  __slots__ = ()


class SWAP(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class POP_JUMP_FORWARD_IF_FALSE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class POP_JUMP_FORWARD_IF_TRUE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class COPY(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class BINARY_OP(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class SEND(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class POP_JUMP_FORWARD_IF_NOT_NONE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class POP_JUMP_FORWARD_IF_NONE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class JUMP_BACKWARD_NO_INTERRUPT(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL | NO_NEXT
  __slots__ = ()


class MAKE_CELL(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_FREE
  __slots__ = ()


class JUMP_BACKWARD(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL | NO_NEXT
  __slots__ = ()


class COPY_FREE_VARS(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class RESUME(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class PRECALL(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class CALL(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class KW_NAMES(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_CONST
  __slots__ = ()


class POP_JUMP_BACKWARD_IF_NOT_NONE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class POP_JUMP_BACKWARD_IF_NONE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class POP_JUMP_BACKWARD_IF_FALSE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class POP_JUMP_BACKWARD_IF_TRUE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class INTERPRETER_EXIT(Opcode):
  __slots__ = ()


class END_FOR(Opcode):
  __slots__ = ()


class END_SEND(Opcode):
  __slots__ = ()


class RESERVED(Opcode):
  __slots__ = ()


class BINARY_SLICE(Opcode):
  __slots__ = ()


class STORE_SLICE(Opcode):
  __slots__ = ()


class CLEANUP_THROW(Opcode):
  __slots__ = ()


class LOAD_LOCALS(Opcode):
  __slots__ = ()


class RETURN_CONST(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_CONST | NO_NEXT
  __slots__ = ()


class LOAD_FAST_CHECK(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_LOCAL
  __slots__ = ()


class POP_JUMP_IF_NOT_NONE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class POP_JUMP_IF_NONE(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_JREL
  __slots__ = ()


class LOAD_SUPER_ATTR(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_NAME
  __slots__ = ()


class LOAD_FAST_AND_CLEAR(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_LOCAL
  __slots__ = ()


class CALL_INTRINSIC_1(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class CALL_INTRINSIC_2(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT
  __slots__ = ()


class LOAD_FROM_DICT_OR_GLOBALS(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_NAME
  __slots__ = ()


class LOAD_FROM_DICT_OR_DEREF(OpcodeWithArg):
  _FLAGS = HAS_ARGUMENT | HAS_FREE
  __slots__ = ()


def _make_opcodes(
    ops: list[pycnite.types.Opcode], python_version: tuple[int, int]
):
  """Convert pycnite opcodes to pytype opcodes."""
  g = globals()
  offset_to_op = {}
  for op in ops:
    cls = g[op.name].for_python_version(python_version)
    if cls.has_argument():
      opcode = cls(0, op.line, op.endline, op.col, op.endcol, op.arg, op.argval)
    else:
      opcode = cls(0, op.line, op.endline, op.col, op.endcol)
    offset_to_op[op.offset] = opcode
  return offset_to_op


def _add_exception_block(offset_to_op, e):
  """Adds opcodes marking an exception block."""
  start_op = offset_to_op[e.start]
  setup_op = SETUP_EXCEPT_311(
      -1, start_op.line, start_op.endline, start_op.col, start_op.endcol, -1, -1
  )
  setup_op.stack_depth = e.depth
  offset_to_op[e.start - 0.5] = setup_op
  target_op = offset_to_op[e.target]
  setup_op.target = target_op
  # Pop the block that we have pushed in SETUP_EXCEPT.
  if e.end not in offset_to_op:
    # e.end is an exclusive boundary in the pyc file; pycnite converts it to
    # an inclusive one by subtracting 2, but that does not always correspond
    # to an op since the wordcode is not strictly one op every two bytes.
    end = max(i for i in offset_to_op if i < e.end)
  else:
    end = e.end
  end_op = offset_to_op[end]
  pop_op = POP_BLOCK(-1, end_op.line, end_op.endline, end_op.col, end_op.endcol)
  offset_to_op[end + 0.5] = pop_op


def _get_exception_bitmask(offset_to_op, exception_ranges):
  """Get a bitmask for whether an offset is in an exception range."""
  in_exception = 0
  pos = 1
  exception_end = None
  for i in range(max(offset_to_op) + 1):
    if i in exception_ranges:
      in_exception += pos
      exception_end = exception_ranges[i]
    elif exception_end is not None:
      in_exception += pos
    if i == exception_end:
      exception_end = None
    pos <<= 1
  return in_exception


# Opcodes that come up as exception targets but don't need a block.
_IGNORED_EXCEPTION_TARGETS = (
    # In 3.11+ `async for` loops end normally by thowing a StopAsyncIteration
    # exception, which jumps to an END_ASYNC_FOR opcode via the exception table.
    END_ASYNC_FOR,
    # In 3.12+ generators end normally by throwing a StopIteration exception,
    # which jumps to a CLEANUP_THROW opcode via the exception table.
    CLEANUP_THROW,
    # In 3.12+ list/dict/set comprehensions jump to a SWAP opcode, which cleans
    # up the stack before re-raising the exception. The cleanup has no effect on
    # type checking.
    SWAP,
)


def _add_setup_except(
    offset_to_op: dict[float, Opcode], exc_table: pycnite.types.ExceptionTable
):
  """Handle the exception table in 3.11+."""
  # In python 3.11, exception handling is no longer bytecode-based - see
  # https://github.com/python/cpython/blob/3.11/Objects/exception_handling_notes.txt
  # This makes it hard for pytype to analyse code containing exceptions, so we
  # add back some opcodes to mark exception blocks.
  #
  # Insert a SETUP_EXCEPT_311 just before the start and a POP_BLOCK just after
  # the end of every exception range.

  # Python 3.11 puts with blocks in the exception table, but the BEFORE_WITH
  # has already set up a block; we don't need to do it with SETUP_EXCEPT.
  # Similarly for async blocks.
  #
  # See test_returns::test_nested_with and test_stdlib2::test_async for
  # examples of complex with/try interactions.
  seen_lines = set()
  exception_ranges = {}
  for e in exc_table.entries:
    if isinstance(offset_to_op[e.target], _IGNORED_EXCEPTION_TARGETS):
      # This entry corresponds to an `async for` block.
      continue
    line = offset_to_op[e.start].line
    if not e.lasti and line not in seen_lines:
      seen_lines.add(line)
      # Entries corresponding to a `with` block have `lasti` set, while the
      # first entry for an exception block does not. So this is an exception.
      _add_exception_block(offset_to_op, e)
      exception_ranges[e.start] = e.end
  # Jumps into and out of exception ranges skip the POP_BLOCK ops we just added,
  # so we mark the jumps so that they can push or pop blocks themselves.
  # NOTE: Our synthetic opcodes added in _add_exception_block have non-integer
  # offsets, and so will not be in the in_exception bitmask, but since we are
  # only concerned with JUMP_* opcodes here it does not matter.
  in_exception = _get_exception_bitmask(offset_to_op, exception_ranges)
  for off, op in offset_to_op.items():
    if not op.has_known_jump() or isinstance(op, SETUP_EXCEPT_311):
      continue
    # `off` is only a float for SETUP_EXCEPT_311 and POP_BLOCK, both are
    # filtered out (POP_BLOCK because it's not a jump).
    off = cast(int, off)
    # Since `op` has a jump, it must have an argument (the jump target).
    op = cast(OpcodeWithArg, op)
    starts_in_exception = (1 << off) & in_exception
    ends_in_exception = (1 << op.argval) & in_exception
    if starts_in_exception and not ends_in_exception:
      op.pop_exc_block = True
    elif ends_in_exception and not starts_in_exception:
      op.push_exc_block = True


def _get_opcode_following_cleanup_throw_jump_pairs(
    op_items: list[tuple[int, Opcode]], start_i: int
):
  for i in range(start_i, len(op_items), 2):
    if (
        isinstance(op_items[i][1], CLEANUP_THROW)
        and i + 1 < len(op_items)
        and isinstance(op_items[i + 1][1], JUMP_BACKWARD)
    ):
      continue
    return op_items[i][1]
  return None


def _should_elide_opcode(
    op_items: list[tuple[int, Opcode]], i: int, python_version: tuple[int, int]
):
  """Returns `True` if the opcode on index `i` should be elided.

  Opcodes should be elided if they don't contribute to type checking and cause
  issues in the block graph.

  Args:
    op_items: List of (offset, opcode) tuples.
    i: Index of opcode to check for elision.
    python_version: Python version tuple.
  """
  op = op_items[i][1]

  # In 3.11 `async for` is compiled into an infinite loop, relying on the
  # exception handler to break out. This causes the block graph to be pruned
  # abruptly, so we need to remove the loop opcode.
  if python_version == (3, 11):
    return (
        isinstance(op, JUMP_BACKWARD)
        and i + 1 < len(op_items)
        and isinstance(op_items[i + 1][1], END_ASYNC_FOR)
    )

  # In 3.12 all generators are compiled into infinite loops, too. In addition,
  # YIELD_VALUE inserts exception handling instructions:
  #     CLEANUP_THROW
  #     JUMP_BACKWARD
  # These can appear on their own or they can be inserted between JUMP_BACKWARD
  # and END_ASYNC_FOR, possibly many times. We keep eliding the `async for` jump
  # and also elide the exception handling cleanup codes because they're not
  # relevant for pytype and complicate the block graph.
  if python_version == (3, 12):
    return (
        isinstance(op, CLEANUP_THROW)
        or (
            isinstance(op, JUMP_BACKWARD)
            and i >= 1
            and isinstance(op_items[i - 1][1], CLEANUP_THROW)
        )
        or (
            isinstance(op, JUMP_BACKWARD)
            and isinstance(
                _get_opcode_following_cleanup_throw_jump_pairs(op_items, i + 1),
                END_ASYNC_FOR,
            )
        )
    )
  return False


def _make_opcode_list(offset_to_op, python_version: tuple[int, int]):
  """Convert opcodes to a list and fill in opcode.index, next and prev."""
  ops = []
  offset_to_index = {}
  prev_op = None
  index = -1
  op_items = sorted(offset_to_op.items())
  for i, (off, op) in enumerate(op_items):
    index += 1
    if _should_elide_opcode(op_items, i, python_version):
      #  We map the offset to the index of the next opcode so that jumps to
      # `op` are redirected correctly.
      offset_to_index[off] = index
      index -= 1
      continue
    op.index = index
    offset_to_index[off] = index
    if prev_op:
      prev_op.next = op
    op.prev = prev_op
    op.next = None
    ops.append(op)
    prev_op = op
  return ops, offset_to_index


def _add_jump_targets(ops, offset_to_index):
  """Map the target of jump instructions to the opcode they jump to."""
  for op in ops:
    op = cast(OpcodeWithArg, op)
    if op.target:
      # We have already set op.target, we need to fill in its index in op.arg
      op.arg = op.argval = op.target.index
    elif op.has_known_jump():
      # op.argval is the postprocessed version of op.arg
      op.arg = op.argval = offset_to_index[op.argval]
      op.target = ops[op.arg]


def build_opcodes(dis_code: pycnite.types.DisassembledCode) -> list[Opcode]:
  """Build a list of opcodes from pycnite opcodes."""
  offset_to_op = _make_opcodes(dis_code.opcodes, dis_code.python_version)
  if dis_code.exception_table:
    _add_setup_except(offset_to_op, dis_code.exception_table)
  ops, offset_to_idx = _make_opcode_list(offset_to_op, dis_code.python_version)
  _add_jump_targets(ops, offset_to_idx)
  return ops


def dis(code: pycnite.types.CodeTypeBase) -> list[Opcode]:
  """Build a list of opcodes from a pycnite CodeType."""
  dis_code = bytecode.dis_all(code)
  return build_opcodes(dis_code)
