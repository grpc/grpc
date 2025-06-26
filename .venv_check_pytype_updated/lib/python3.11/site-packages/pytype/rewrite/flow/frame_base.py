"""Base implementation of a frame of an abstract virtual machine for bytecode.

This module contains a FrameBase class, which provides a base implementation of
a frame that analyzes bytecode one instruction (i.e., opcode) at a time,
tracking variables and conditions. Use FrameBase by subclassing it and adding a
byte_{opcode_name} method implementing each opcode.
"""

from collections.abc import Mapping
import dataclasses
import logging
from typing import Generic, TypeVar

from pytype.blocks import blocks
from pytype.pyc import opcodes
from pytype.rewrite.flow import state
from pytype.rewrite.flow import variables

_T = TypeVar('_T')

log = logging.getLogger(__name__)

_FINAL = -1


@dataclasses.dataclass
class _Step:
  """Block and opcode indices for a frame step."""

  block: int
  opcode: int


class FrameConsumedError(Exception):
  """Raised when step() is called on a frame with no more opcodes to execute."""


class FrameBase(Generic[_T]):
  """Virtual machine frame.

  Attributes:
    _final_locals: The frame's `locals` dictionary after it finishes execution.
      This is a protected attribute so that subclasses can choose whether and
      how to expose control flow information.
    current_opcode: The current opcode.
  """

  def __init__(
      self, code: blocks.OrderedCode,
      initial_locals: Mapping[str, variables.Variable[_T]],
  ):
    # Sanity check: non-empty code
    assert code.order and all(block.code for block in code.order)
    self._code = code  # bytecode

    # Local names before and after frame runs
    self._initial_locals = initial_locals
    self._final_locals: Mapping[str, variables.Variable[_T]] = None

    self._current_step = _Step(0, 0)  # current block and opcode indices

    self._states: dict[int, state.BlockState[_T]] = {}  # block id to state
    # Initialize the state of the first block.
    self._states[self._code.order[0].id] = state.BlockState(
        locals_=dict(self._initial_locals))
    self._current_state: state.BlockState[_T] = None  # state of current block

  @property
  def current_opcode(self) -> opcodes.Opcode:
    return self._code.order[self._current_step.block][self._current_step.opcode]

  def step(self) -> None:
    """Runs one opcode."""
    # Grab the current block and opcode.
    block_index = self._current_step.block
    if block_index == _FINAL:
      raise FrameConsumedError()
    opcode_index = self._current_step.opcode
    block = self._code.order[block_index]
    opcode = block[opcode_index]
    # Grab the block's initial state.
    self._current_state = self._states[block.id]
    if not opcode_index:
      log.info('Entering block: %d', block.id)
    # Run the opcode.
    opname = opcode.__class__.__name__
    try:
      op_impl = getattr(self, f'byte_{opname}')
    except AttributeError as e:
      raise NotImplementedError(f'Opcode {opname} not implemented') from e
    log.info(str(opcode))
    op_impl(opcode)
    # Update current block and opcode.
    if opcode is not block[-1]:
      self._current_step.opcode += 1
      return
    log.info('Exiting block: %d', block.id)
    if not opcode.carry_on_to_next():
      # Update the frame's final state.
      self._merge_state_into(self._current_state, _FINAL)
    elif not opcode.has_known_jump():
      # Merge the current state into the next.
      self._merge_state_into(self._current_state, opcode.next.index)
    if block is self._code.order[-1]:
      self._current_step.block = _FINAL
      self._final_locals = self._states[_FINAL].get_locals()
    else:
      self._current_step.block += 1
      self._current_step.opcode = 0

  def stepn(self, n: int) -> None:
    """Runs n opcodes."""
    for _ in range(n):
      self.step()

  def _merge_state_into(
      self, from_state: state.BlockState[_T], block_id: int) -> None:
    self._states[block_id] = from_state.merge_into(self._states.get(block_id))
