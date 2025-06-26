"""Overlays on top of abstract values that provide extra typing information.

An overlay generates extra typing information that cannot be expressed in a pyi
file. For example, collections.namedtuple is a factory method that generates
class definitions at runtime. An overlay is used to generate these classes.
"""
from collections.abc import Callable
from typing import TypeVar

from pytype.rewrite.abstract import abstract

_FuncTypeType = type[abstract.PytdFunction]
_FuncTypeTypeT = TypeVar('_FuncTypeTypeT', bound=_FuncTypeType)
_ClsTransformFunc = Callable[[abstract.ContextType, abstract.SimpleClass], None]
_ClsTransformFuncT = TypeVar('_ClsTransformFuncT', bound=_ClsTransformFunc)

FUNCTIONS: dict[tuple[str, str], _FuncTypeType] = {}
CLASS_TRANSFORMS: dict[str, _ClsTransformFunc] = {}


def register_function(
    module: str, name: str) -> Callable[[_FuncTypeTypeT], _FuncTypeTypeT]:
  def register(func_builder: _FuncTypeTypeT) -> _FuncTypeTypeT:
    FUNCTIONS[(module, name)] = func_builder
    return func_builder
  return register


def register_class_transform(
    *, inheritance_hook: str
) -> Callable[[_ClsTransformFuncT], _ClsTransformFuncT]:
  def register(transformer: _ClsTransformFuncT) -> _ClsTransformFuncT:
    CLASS_TRANSFORMS[inheritance_hook] = transformer
    return transformer
  return register


def initialize():
  # Imports overlay implementations so that ther @register_* decorators execute
  # and populate the overlay registry.
  # pylint: disable=g-import-not-at-top,unused-import
  # pytype: disable=import-error
  from pytype.rewrite.overlays import enum_overlay
  from pytype.rewrite.overlays import special_builtins
  # pytype: enable=import-error
  # pylint: enable=g-import-not-at-top,unused-import
