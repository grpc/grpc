"""Utilities for working with abstract values."""

from collections.abc import Sequence
from typing import Any, TypeVar, get_origin, overload

from pytype.rewrite.abstract import base


_Var = base.AbstractVariableType
_T = TypeVar('_T')


@overload
def get_atomic_constant(var: _Var, typ: type[_T]) -> _T:
  ...


@overload
def get_atomic_constant(var: _Var, typ: None = ...) -> Any:
  ...


def get_atomic_constant(var, typ=None):
  value = var.get_atomic_value(base.PythonConstant)
  constant = value.constant
  if typ and not isinstance(constant, (runtime_type := get_origin(typ) or typ)):
    raise ValueError(
        f'Wrong constant type for {var.display_name()}: expected '
        f'{runtime_type.__name__}, got {constant.__class__.__name__}')
  return constant


def join_values(
    ctx: base.ContextType, values: Sequence[base.BaseValue]) -> base.BaseValue:
  if len(values) > 1:
    return base.Union(ctx, values)
  elif values:
    return values[0]
  else:
    return ctx.consts.Any


def is_any(value: base.BaseValue):
  return isinstance(value, base.Singleton) and value.name == 'Any'
