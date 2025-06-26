"""Abstract representations of builtin containers."""

import logging

from pytype.rewrite.abstract import base
from pytype.rewrite.abstract import internal
from pytype.rewrite.abstract import utils

log = logging.getLogger(__name__)

# Type aliases
_Var = base.AbstractVariableType


class List(base.PythonConstant[list[_Var]]):
  """Representation of a Python list."""

  def __init__(self, ctx: base.ContextType, constant: list[_Var]):
    assert isinstance(constant, list), constant
    super().__init__(ctx, constant)

  def __repr__(self):
    return f'List({self.constant!r})'

  def append(self, var: _Var) -> 'List':
    return List(self._ctx, self.constant + [var])

  def extend(self, val: 'List') -> 'List':
    new_constant = self.constant + val.constant
    return List(self._ctx, new_constant)


class Dict(base.PythonConstant[dict[_Var, _Var]]):
  """Representation of a Python dict."""

  def __init__(
      self,
      ctx: base.ContextType,
      constant: dict[_Var, _Var],
  ):
    assert isinstance(constant, dict), constant
    super().__init__(ctx, constant)

  def __repr__(self):
    return f'Dict({self.constant!r})'

  @classmethod
  def from_function_arg_dict(
      cls, ctx: base.ContextType, val: internal.FunctionArgDict
  ) -> 'Dict':
    assert not val.indefinite
    new_constant = {
        ctx.consts[k].to_variable(): v
        for k, v in val.constant.items()
    }
    return cls(ctx, new_constant)

  def setitem(self, key: _Var, val: _Var) -> 'Dict':
    return Dict(self._ctx, {**self.constant, key: val})

  def update(self, val: 'Dict') -> base.BaseValue:
    return Dict(self._ctx, {**self.constant, **val.constant})

  def to_function_arg_dict(self) -> internal.FunctionArgDict:
    new_const = {
        utils.get_atomic_constant(k, str): v
        for k, v in self.constant.items()
    }
    return internal.FunctionArgDict(self._ctx, new_const)


class Set(base.PythonConstant[set[_Var]]):
  """Representation of a Python set."""

  def __init__(self, ctx: base.ContextType, constant: set[_Var]):
    assert isinstance(constant, set), constant
    super().__init__(ctx, constant)

  def __repr__(self):
    return f'Set({self.constant!r})'

  def add(self, val: _Var) -> 'Set':
    return Set(self._ctx, self.constant | {val})


class Tuple(base.PythonConstant[tuple[_Var, ...]]):
  """Representation of a Python tuple."""

  def __init__(self, ctx: base.ContextType, constant: tuple[_Var, ...]):
    assert isinstance(constant, tuple), constant
    super().__init__(ctx, constant)

  def __repr__(self):
    return f'Tuple({self.constant!r})'
