"""Base abstract representation of Python values."""

import abc
from collections.abc import Sequence
from typing import Any, Generic, Optional, Protocol, TypeVar

from pytype import config
from pytype import load_pytd
from pytype import utils
from pytype.pytd import pytd
from pytype.rewrite.flow import variables
from pytype.types import types
from typing_extensions import Self

_T = TypeVar('_T')


class ContextType(Protocol):

  options: config.Options
  pytd_loader: load_pytd.Loader

  errorlog: Any
  abstract_converter: Any
  abstract_loader: Any
  pytd_converter: Any
  consts: Any
  types: Any


class BaseValue(types.BaseValue, abc.ABC):
  """Base class for abstract values."""

  # For convenience, we want the 'name' attribute to be available on all values.
  # Setting it as a class attribute gives subclasses the most flexibility in how
  # to define it.
  name = ''

  def __init__(self, ctx: ContextType):
    self._ctx = ctx

  @abc.abstractmethod
  def __repr__(self):
    ...

  @property
  @abc.abstractmethod
  def _attrs(self) -> tuple[Any, ...]:
    """This object's identifying attributes.

    Used for equality comparisons and hashing. Should return a tuple of the
    attributes needed to differentiate this object from others of the same type.
    The attributes must be hashable. Do not include the type of `self` or
    `self._ctx`.
    """

  @property
  def full_name(self):
    return self.name

  def __eq__(self, other):
    return self.__class__ == other.__class__ and self._attrs == other._attrs

  def __hash__(self):
    return hash((self.__class__, self._ctx) + self._attrs)

  def to_variable(self, name: str | None = None) -> variables.Variable[Self]:
    return variables.Variable.from_value(self, name=name)

  def get_attribute(self, name: str) -> Optional['BaseValue']:
    del name  # unused
    return None

  def set_attribute(self, name: str, value: 'BaseValue') -> None:
    del name, value  # unused

  def instantiate(self) -> 'BaseValue':
    """Creates an instance of this value."""
    raise ValueError(f'{self!r} is not instantiable')

  def to_pytd_def(self) -> pytd.Node:
    return self._ctx.pytd_converter.to_pytd_def(self)

  def to_pytd_type(self) -> pytd.Type:
    return self._ctx.pytd_converter.to_pytd_type(self)

  def to_pytd_type_of_instance(self) -> pytd.Type:
    return self._ctx.pytd_converter.to_pytd_type_of_instance(self)


class PythonConstant(BaseValue, Generic[_T]):
  """Representation of a Python constant.

  DO NOT INSTANTIATE THIS CLASS DIRECTLY! Doing so will create extra copies of
  constants, potentially causing subtle bugs. Instead, fetch the canonical
  instance of the constant using ctx.consts[constant].
  """

  def __init__(
      self, ctx: ContextType, constant: _T, allow_direct_instantiation=False):
    if self.__class__ is PythonConstant and not allow_direct_instantiation:
      raise ValueError('Do not instantiate PythonConstant directly. Use '
                       'ctx.consts[constant] instead.')
    super().__init__(ctx)
    self.constant = constant

  def __repr__(self):
    return f'PythonConstant({self.constant!r})'

  @property
  def _attrs(self):
    return (self.constant,)


class Singleton(BaseValue):
  """Singleton value.

  DO NOT INSTANTIATE THIS CLASS DIRECTLY! Doing so will create extra copies of
  singletons, potentially causing subtle bugs. Instead, fetch the canonical
  instance of the singleton using ctx.consts.singles[name].
  """

  name: str

  def __init__(self, ctx, name, allow_direct_instantiation=False):
    if self.__class__ is Singleton and not allow_direct_instantiation:
      raise ValueError('Do not instantiate Singleton directly. Use '
                       'ctx.consts.singles[name] instead.')
    super().__init__(ctx)
    self.name = name

  def __repr__(self):
    return self.name

  @property
  def _attrs(self):
    return (self.name,)

  def instantiate(self) -> 'Singleton':
    return self

  def get_attribute(self, name: str) -> 'Singleton':
    return self


class Union(BaseValue):
  """Union of values."""

  def __init__(self, ctx: ContextType, options: Sequence[BaseValue]):
    super().__init__(ctx)
    assert len(options) > 1
    flattened_options = []
    for o in options:
      if isinstance(o, Union):
        flattened_options.extend(o.options)
      else:
        flattened_options.append(o)
    self.options = tuple(utils.unique_list(flattened_options))

  def __repr__(self):
    return ' | '.join(repr(o) for o in self.options)

  @property
  def _attrs(self):
    return (frozenset(self.options),)

  def instantiate(self):
    return Union(self._ctx, tuple(o.instantiate() for o in self.options))


AbstractVariableType = variables.Variable[BaseValue]
