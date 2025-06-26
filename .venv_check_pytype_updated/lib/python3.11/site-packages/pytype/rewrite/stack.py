"""Data stack."""

from collections.abc import Sequence

from pytype.rewrite.abstract import abstract
from pytype.rewrite.flow import variables


_Var = variables.Variable[abstract.BaseValue]  # typing alias


class DataStack:
  """Data stack."""

  def __init__(self):
    self._stack: list[_Var] = []

  def push(self, var: _Var) -> None:
    self._stack.append(var)

  def pop(self) -> _Var:
    return self._stack.pop()

  def popn(self, n: int) -> Sequence[_Var]:
    if not n:
      return ()
    if len(self._stack) < n:
      self._stack_size_error(f'pop {n} values')
    values = self._stack[-n:]
    self._stack = self._stack[:-n]
    return values

  def pop_and_discard(self) -> None:
    _ = self._stack.pop()

  def rotn(self, n: int) -> None:
    """Rotate the top n values by one."""
    if n <= 1:
      raise IndexError(f'rotn(n) requires n > 1, got: {n}')
    if len(self._stack) < n:
      self._stack_size_error(f'rotate {n} values')
    top = self._stack[-1]
    rot = self._stack[-n:-1]
    self._stack = self._stack[:-n] + [top] + rot

  def top(self) -> _Var:
    return self._stack[-1]

  def peek(self, n: int) -> _Var:
    if n <= 0:
      raise IndexError(f'peek(n) requires positive n, got: {n}')
    if n > len(self._stack):
      self._stack_size_error(f'peek value {n} places down')
    return self._stack[-n]

  def replace(self, n: int, var: _Var) -> None:
    if n <= 0:
      raise IndexError(f'replace(n) requires positive n, got: {n}')
    if n > len(self._stack):
      self._stack_size_error(f'replace value {n} places down')
    self._stack[-n] = var

  def _stack_size_error(self, msg):
    msg = f'Trying to {msg} in a stack of size {len(self._stack)}'
    raise IndexError(msg)

  def __bool__(self):
    return bool(self._stack)

  def __len__(self):
    return len(self._stack)

  def __repr__(self):
    return f'DataStack{self._stack}'
