"""Bytecode block state."""

from collections.abc import Mapping
from typing import Generic, Optional, TypeVar

import immutabledict
from pytype.rewrite.flow import conditions
from pytype.rewrite.flow import variables

_T = TypeVar('_T')


class BlockState(Generic[_T]):
  """State of a bytecode block."""

  def __init__(
      self,
      locals_: dict[str, variables.Variable[_T]],
      condition: conditions.Condition = conditions.TRUE,
      locals_with_block_condition: set[str] | None = None,
  ):
    self._locals = locals_
    self._condition = condition
    # Performance optimization: whenever possible, we track conditions on the
    # block rather than individual variables for faster updates.
    if locals_with_block_condition is None:
      self._locals_with_block_condition = set(locals_)
    else:
      self._locals_with_block_condition = locals_with_block_condition

  def __repr__(self):
    return (f'BlockState(locals={self._locals}, condition={self._condition}, '
            f'locals_with_block_condition={self._locals_with_block_condition})')

  def load_local(self, name: str) -> variables.Variable[_T]:
    return self._locals[name].with_name(name)

  def store_local(self, name: str, var: variables.Variable[_T]) -> None:
    self._locals[name] = var
    self._locals_with_block_condition.add(name)

  def get_locals(self) -> Mapping[str, variables.Variable[_T]]:
    return immutabledict.immutabledict(self._locals)

  def with_condition(self, condition: conditions.Condition) -> 'BlockState[_T]':
    """Creates a new state with the given condition 'and'-ed in."""
    condition = conditions.And(self._condition, condition)
    new_locals = {}
    for name, var in self._locals.items():
      if name in self._locals_with_block_condition:
        new_locals[name] = var
      else:
        new_locals[name] = var.with_condition(condition)
    return BlockState(
        locals_=new_locals,
        condition=condition,
        locals_with_block_condition=set(self._locals_with_block_condition),
    )

  def merge_into(self, other: Optional['BlockState[_T]']) -> 'BlockState[_T]':
    """Merges 'self' into 'other', 'or'-ing conditions."""
    if not other:
      return BlockState(
          locals_=dict(self._locals),
          condition=self._condition,
          locals_with_block_condition=set(self._locals_with_block_condition),
      )
    # A merge of two blocks needs access to private attributes on both:
    # pylint: disable=protected-access
    condition = conditions.Or(self._condition, other._condition)
    locals_ = {}
    locals_with_block_condition = set()
    for name, var in self._locals.items():
      if name in other._locals and var == other._locals[name]:
        # This variable did not pick up any value specific to either block, so
        # give it the merged block's condition.
        locals_[name] = var
        locals_with_block_condition.add(name)
      elif name in self._locals_with_block_condition:
        # This variable implicitly had a block condition, which we now
        # explicitly add to the variable.
        locals_[name] = var.with_condition(self._condition)
      else:
        locals_[name] = var
    for name, var in other._locals.items():
      if name in locals_with_block_condition:
        continue
      if name in other._locals_with_block_condition:
        # This variable implicitly had a block condition, which we now
        # explicitly add to the variable.
        var = var.with_condition(other._condition)
      if name not in locals_:
        locals_[name] = var
        continue
      # Both blocks define this variable, so merge the two sets of bindings.
      bindings = {b.value: b.condition for b in locals_[name].bindings}
      for b in var.bindings:
        if b.value in bindings:
          bindings[b.value] = conditions.Or(bindings[b.value], b.condition)
        else:
          bindings[b.value] = b.condition
      locals_[name] = variables.Variable(
          tuple(variables.Binding(k, v) for k, v in bindings.items()))
    # pylint: enable=protected-access
    return BlockState(locals_, condition, locals_with_block_condition)
