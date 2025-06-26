"""Function definitions in pyi files."""

import ast as astlib
import dataclasses
import textwrap
from typing import Any, cast

from pytype.pyi import types
from pytype.pytd import pytd
from pytype.pytd import visitors
from pytype.pytd.codegen import function as pytd_function
from pytype.pytd.parse import parser_constants

_ParseError = types.ParseError


class Mutator(visitors.Visitor):
  """Visitor for adding a mutated_type to parameters.

  We model the parameter x in
    def f(x: old_type):
      x = new_type
  as
    Parameter(name=x, type=old_type, mutated_type=new_type)
  .
  This visitor applies the body "x = new_type" to the function signature.
  """

  def __init__(self, name, new_type):
    super().__init__()
    self.name = name
    self.new_type = new_type
    self.successful = False

  def VisitParameter(self, p):
    if p.name == self.name:
      self.successful = True
      if p.optional:
        raise NotImplementedError(
            f"Argument {p.name!r} cannot be both mutable and optional"
        )
      return p.Replace(mutated_type=self.new_type)
    else:
      return p

  def __repr__(self):
    return f"Mutator<{self.name} -> {self.new_type}>"

  __str__ = __repr__


class Param(pytd_function.Param):
  """Internal representation of function parameters."""

  @classmethod
  def from_arg(cls, arg: astlib.arg, kind: pytd.ParameterKind) -> "Param":
    """Constructor from an ast.argument node."""
    p = cls(arg.arg)
    if arg.annotation:
      p.type = arg.annotation
    p.kind = kind
    return p


@dataclasses.dataclass
class SigProperties:
  abstract: bool
  coroutine: bool
  final: bool
  overload: bool
  is_async: bool


class NameAndSig(pytd_function.NameAndSig):
  """Internal representation of function signatures."""

  @classmethod
  def from_function(
      cls, function: astlib.FunctionDef, props: SigProperties
  ) -> "NameAndSig":
    """Constructor from an ast.FunctionDef node."""
    name = function.name

    decorators = cast(list[pytd.Alias], function.decorator_list)
    mutually_exclusive = {"property", "staticmethod", "classmethod"}
    if len({d.type.name for d in decorators} & mutually_exclusive) > 1:
      raise _ParseError(
          f"'{name}' can be decorated with at most one of "
          "property, staticmethod, and classmethod"
      )

    exceptions = []
    mutators = []
    for i, x in enumerate(function.body):
      if isinstance(x, types.Raise):
        exceptions.append(x.exception)
      elif isinstance(x, Mutator):
        mutators.append(x)
      elif isinstance(x, types.Ellipsis):
        pass
      elif (
          isinstance(x, astlib.Expr)
          and isinstance(x.value, astlib.Constant)
          and isinstance(x.value.value, str)
          and i == 0
      ):
        # docstring
        pass
      else:
        msg = textwrap.dedent("""
            Unexpected statement in function body.
            Only `raise` statements and type mutations are valid
        """).lstrip()
        if isinstance(x, astlib.AST):
          raise _ParseError(msg).at(x)
        else:
          raise _ParseError(msg)

    # exceptions
    sig = _pytd_signature(function, props.is_async, exceptions=exceptions)

    # mutators
    # If `self` is generic, a type parameter is being mutated.
    if (
        sig.params
        and sig.params[0].name == "self"
        and isinstance(sig.params[0].type, pytd.GenericType)
    ):
      mutators.append(Mutator("self", sig.params[0].type))
    for mutator in mutators:
      try:
        sig = sig.Visit(mutator)
      except NotImplementedError as e:
        raise _ParseError(str(e)) from e
      if not mutator.successful:
        raise _ParseError(f"No parameter named {mutator.name!r}")

    return cls(
        name,
        sig,
        tuple(decorators),
        props.abstract,
        props.coroutine,
        props.final,
        props.overload,
    )


def _pytd_signature(
    function: astlib.FunctionDef,
    is_async: bool,
    exceptions: list[pytd.Type] | None = None,
) -> pytd.Signature:
  """Construct a pytd signature from an ast.FunctionDef node."""
  name = function.name
  args = function.args
  # Positional-only parameters are new in Python 3.8.
  posonly_params = [
      Param.from_arg(a, pytd.ParameterKind.POSONLY)
      for a in getattr(args, "posonlyargs", ())
  ]
  pos_params = [
      Param.from_arg(a, pytd.ParameterKind.REGULAR) for a in args.args
  ]
  kwonly_params = [
      Param.from_arg(a, pytd.ParameterKind.KWONLY) for a in args.kwonlyargs
  ]
  _apply_defaults(posonly_params + pos_params, args.defaults)
  _apply_defaults(kwonly_params, args.kw_defaults)
  all_params = posonly_params + pos_params + kwonly_params
  params = tuple(x.to_pytd() for x in all_params)
  starargs = _pytd_star_param(args.vararg)
  starstarargs = _pytd_starstar_param(args.kwarg)
  ret = pytd_function.pytd_return_type(name, function.returns, is_async)
  exceptions = exceptions or []
  return pytd.Signature(
      params=params,
      return_type=ret,
      starargs=starargs,
      starstarargs=starstarargs,
      exceptions=tuple(exceptions),
      template=(),
  )


def _pytd_star_param(arg: astlib.arg) -> pytd.Parameter | None:
  """Return a pytd.Parameter for a *args argument."""
  if not arg:
    return None
  # Temporary hack: until pytype supports Unpack, treat it as Any.
  unpack = parser_constants.EXTERNAL_NAME_PREFIX + "typing_extensions.Unpack"
  if (
      isinstance(arg.annotation, pytd.GenericType)
      and arg.annotation.base_type.name == unpack
  ):
    arg.annotation = pytd.AnythingType()
  return pytd_function.pytd_star_param(arg.arg, arg.annotation)  # pytype: disable=wrong-arg-types


def _pytd_starstar_param(arg: astlib.arg | None) -> pytd.Parameter | None:
  """Return a pytd.Parameter for a **kwargs argument."""
  if not arg:
    return None
  return pytd_function.pytd_starstar_param(arg.arg, arg.annotation)  # pytype: disable=wrong-arg-types


def _apply_defaults(params: list[Param], defaults: list[Any]) -> None:
  for p, d in zip(reversed(params), reversed(defaults)):
    if d is None:
      continue
    elif isinstance(d, types.Pyval):
      p.default = d.to_pytd()
    else:
      p.default = pytd.AnythingType()
