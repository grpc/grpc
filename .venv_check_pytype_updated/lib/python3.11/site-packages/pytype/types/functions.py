"""Basic datatypes for function definitions and call args."""

import abc
import dataclasses

from pytype.types import base


@dataclasses.dataclass
class Signature(abc.ABC):
  """Representation of a Python function signature.

  Attributes:
    name: Name of the function.
    param_names: A tuple of positional parameter names. This DOES include
      positional-only parameters and does NOT include keyword-only parameters.
    posonly_count: Number of positional-only parameters. (Python 3.8)
    varargs_name: Name of the varargs parameter. (The "args" in *args)
    kwonly_params: Tuple of keyword-only parameters. (Python 3) E.g. ("x", "y")
      for "def f(a, *, x, y=2)". These do NOT appear in param_names. Ordered
      like in the source file.
    kwargs_name: Name of the kwargs parameter. (The "kwargs" in **kwargs)
    posonly_params: Tuple of positional-only parameters
  """

  name: str
  param_names: tuple[str, ...]
  posonly_count: int
  varargs_name: str | None
  kwonly_params: tuple[str, ...]
  kwargs_name: str | None

  @property
  def posonly_params(self):
    return self.param_names[: self.posonly_count]

  @abc.abstractmethod
  def has_default(self, name):
    """Whether the named arg has a default value."""

  @abc.abstractmethod
  def insert_varargs_and_kwargs(self, args):
    """Insert varargs and kwargs from args into the signature."""

  @abc.abstractmethod
  def iter_args(self, args):
    """Iterates through the given args, attaching names and expected types."""


@dataclasses.dataclass(eq=True, frozen=True)
class Args:
  """Represents the parameters of a function call.

  Attributes:
    posargs: The positional arguments. A tuple of base.Variable.
    namedargs: The keyword arguments. A dictionary, mapping strings to
      base.Variable.
    starargs: The *args parameter, or None.
    starstarargs: The **kwargs parameter, or None.
  """

  posargs: tuple[base.Variable, ...]
  namedargs: dict[str, base.Variable]
  starargs: base.Variable | None = None
  starstarargs: base.Variable | None = None


@dataclasses.dataclass(eq=True, frozen=True)
class Arg:
  """A single function argument. Used in the matcher and for error handling."""

  name: str
  value: base.Variable
  typ: base.BaseValue


class Function(base.BaseValue):
  """Base class for representation of python functions."""

  is_overload: bool
  name: str
  decorators: list[str]

  def signatures(self) -> list[Signature]:
    """All signatures of this function."""
    raise NotImplementedError()
