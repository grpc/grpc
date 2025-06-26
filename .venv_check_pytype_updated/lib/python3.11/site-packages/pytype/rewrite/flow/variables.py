"""Variables, bindings, and conditions."""

import dataclasses
from typing import Any, Generic, TypeVar, get_origin, overload

from pytype.rewrite.flow import conditions

_frozen_dataclass = dataclasses.dataclass(frozen=True)
_T = TypeVar('_T')
_T2 = TypeVar('_T2')


@_frozen_dataclass
class Binding(Generic[_T]):
  """A binding to a value that applies when a condition is satisfied."""

  value: _T
  condition: conditions.Condition = conditions.TRUE

  def __repr__(self):
    if self.condition is conditions.TRUE:
      return f'Bind[{self.value}]'
    return f'Bind[{self.value} if {self.condition}]'

  @property
  def data(self):
    # Temporary alias for 'value' for compatibility with current pytype.
    return self.value


@_frozen_dataclass
class Variable(Generic[_T]):
  """A collection of bindings, optionally named."""

  bindings: tuple[Binding[_T], ...]
  name: str | None = None

  @classmethod
  def from_value(
      cls, value: _T2, *, name: str | None = None
  ) -> 'Variable[_T2]':
    return cls(bindings=(Binding(value),), name=name)

  @property
  def values(self) -> tuple[_T, ...]:
    return tuple(b.value for b in self.bindings)

  @property
  def data(self):
    # Temporary alias for 'values' for compatibility with current pytype.
    return self.values

  def display_name(self) -> str:
    return f'variable {self.name}' if self.name else 'anonymous variable'

  @overload
  def get_atomic_value(self, typ: type[_T2]) -> _T2:
    ...

  @overload
  def get_atomic_value(self, typ: None = ...) -> _T:
    ...

  def get_atomic_value(self, typ=None):
    """Gets this variable's value if there's exactly one, errors otherwise."""
    if not self.is_atomic():
      desc = 'many' if len(self.bindings) > 1 else 'few'
      raise ValueError(
          f'Too {desc} bindings for {self.display_name()}: {self.bindings}')
    value = self.bindings[0].value
    if typ and not isinstance(value, (runtime_type := get_origin(typ) or typ)):
      raise ValueError(
          f'Wrong type for {self.display_name()}: expected '
          f'{runtime_type.__name__}, got {value.__class__.__name__}')
    return value

  def is_atomic(self, typ: type[_T] | None = None) -> bool:
    if len(self.bindings) != 1:
      return False
    return True if typ is None else isinstance(self.values[0], typ)

  def has_atomic_value(self, value: Any) -> bool:
    return self.is_atomic() and self.values[0] == value

  def with_condition(self, condition: conditions.Condition) -> 'Variable[_T]':
    """Adds a condition, 'and'-ing it with any existing."""
    if condition is conditions.TRUE:
      return self
    new_bindings = []
    for b in self.bindings:
      new_condition = conditions.And(b.condition, condition)
      new_bindings.append(dataclasses.replace(b, condition=new_condition))
    return dataclasses.replace(self, bindings=tuple(new_bindings))

  def with_name(self, name: str | None) -> 'Variable[_T]':
    return dataclasses.replace(self, name=name)

  def with_value(self, value: _T2) -> 'Variable[_T2]':
    assert len(self.bindings) == 1
    new_binding = dataclasses.replace(self.bindings[0], value=value)
    return dataclasses.replace(self, bindings=(new_binding,))

  def __repr__(self):
    bindings = ' | '.join(repr(b) for b in self.bindings)
    if self.name:
      return f'Var[{self.name} -> {bindings}]'
    return f'Var[{bindings}]'
