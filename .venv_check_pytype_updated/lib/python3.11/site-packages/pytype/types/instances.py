"""Basic datatypes for instances."""

from collections.abc import Callable
from typing import Any

from pytype.types import base


class Module:
  name: str


class PythonConstant:
  pyval: Any
  is_concrete: bool

  def str_of_constant(self, printer: Callable[[base.BaseValue], str]) -> str:
    raise NotImplementedError()
