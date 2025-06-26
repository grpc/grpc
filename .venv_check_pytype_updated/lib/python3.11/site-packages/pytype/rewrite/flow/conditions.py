"""Variables, bindings, and conditions."""

import dataclasses
from typing import ClassVar

_frozen_dataclass = dataclasses.dataclass(frozen=True)


@_frozen_dataclass
class Condition:
  """A condition that must be satisified for a binding to apply."""


@_frozen_dataclass
class _True(Condition):

  def __repr__(self):
    return 'TRUE'


@_frozen_dataclass
class _False(Condition):

  def __repr__(self):
    return 'FALSE'


TRUE = _True()
FALSE = _False()


@_frozen_dataclass
class _Not(Condition):
  """Negation of a condition."""

  condition: Condition

  def __repr__(self):
    return f'not {self.condition}'

  @classmethod
  def make(cls, condition: Condition) -> Condition:
    if isinstance(condition, _Not):
      return condition.condition
    return cls(condition)


Not = _Not.make


@_frozen_dataclass
class _Composite(Condition):
  """Composition of conditions."""

  conditions: frozenset[Condition]

  _ACCEPT: ClassVar[Condition]
  _IGNORE: ClassVar[Condition]
  _REPR: ClassVar[str]

  @classmethod
  def make(cls, *args: Condition) -> Condition:
    """Composes the input conditions."""
    conditions = set()
    for arg in args:
      if arg is cls._IGNORE:
        continue
      if arg is cls._ACCEPT:
        return arg
      negation = Not(arg)
      if negation in conditions:
        return cls._ACCEPT
      conditions.add(arg)
    if not conditions:
      return cls._IGNORE
    if len(conditions) == 1:
      return conditions.pop()
    return cls(frozenset(conditions))

  def __repr__(self):
    conditions = []
    for c in self.conditions:
      if isinstance(c, _Composite):
        conditions.append(f'({repr(c)})')
      else:
        conditions.append(repr(c))
    return f' {self._REPR} '.join(conditions)


@dataclasses.dataclass(frozen=True, repr=False)
class _Or(_Composite):

  _ACCEPT: ClassVar[Condition] = TRUE
  _IGNORE: ClassVar[Condition] = FALSE
  _REPR: ClassVar[str] = 'or'


@dataclasses.dataclass(frozen=True, repr=False)
class _And(_Composite):

  _ACCEPT: ClassVar[Condition] = FALSE
  _IGNORE: ClassVar[Condition] = TRUE
  _REPR: ClassVar[str] = 'and'


Or = _Or.make
And = _And.make
