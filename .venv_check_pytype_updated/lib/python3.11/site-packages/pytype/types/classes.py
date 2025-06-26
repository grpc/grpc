"""Basic datatypes for classes."""

import abc
from collections.abc import Mapping, Sequence
import dataclasses
from typing import Any

from pytype.types import base


class Class(base.BaseValue, abc.ABC):
  """Base class for representation of python classes."""

  @property
  @abc.abstractmethod
  def name(self) -> str:
    """Class name."""

  @property
  @abc.abstractmethod
  def bases(self) -> Sequence[base.BaseValue]:
    """Class bases."""

  @property
  @abc.abstractmethod
  def mro(self) -> Sequence[base.BaseValue]:
    """Class MRO."""

  @property
  @abc.abstractmethod
  def metaclass(self) -> base.BaseValue:
    """Class metaclass."""

  @property
  @abc.abstractmethod
  def decorators(self) -> Sequence[str]:
    """Class decoratotrs."""

  @property
  @abc.abstractmethod
  def members(self) -> Mapping[str, base.BaseValue]:
    """Class members."""

  @property
  @abc.abstractmethod
  def metadata(self) -> Mapping[str, Any]:
    """Metadata used in overlays and other metaprogramming contexts."""


@dataclasses.dataclass
class Attribute:
  """Represents a class member variable.

  Members:
    name: field name
    typ: field python type
    init: Whether the field should be included in the generated __init__
    kw_only: Whether the field is kw_only in the generated __init__
    default: Default value
    kind: Kind of attribute

  Used in overlays that reflect on class attributes.
  """

  name: str
  typ: Any
  init: bool
  kw_only: bool
  default: Any
  kind: str = ""
  # TODO(mdemello): Are these tied to the current implementation?
  init_type: Any = None
  pytd_const: Any = None
