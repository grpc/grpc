"""Types for structured errors."""

from collections.abc import Sequence
import dataclasses
from typing import Optional

from pytype.types import types


class ReturnValueMixin:
  """Mixin for exceptions that hold a return node and variable."""

  def __init__(self):
    super().__init__()
    self.return_node = None
    self.return_variable = None

  def set_return(self, node, var):
    self.return_node = node
    self.return_variable = var

  def get_return(self, state):
    return state.change_cfg_node(self.return_node), self.return_variable


@dataclasses.dataclass(eq=True, frozen=True)
class BadType:
  name: str | None
  typ: types.BaseValue
  error_details: Optional["MatcherErrorDetails"] = None


# These names are chosen to match pytype error classes.
# pylint: disable=g-bad-exception-name
#
# --------------------------------------------------------
# Function call errors


class FailedFunctionCall(Exception, ReturnValueMixin):
  """Exception for failed function calls."""

  def __init__(self):
    super().__init__()
    self.name = "<no name>"

  def __gt__(self, other):
    return other is None

  def __le__(self, other):
    return not self.__gt__(other)


class NotCallable(FailedFunctionCall):
  """For objects that don't have __call__."""

  def __init__(self, obj):
    super().__init__()
    self.obj = obj


class UndefinedParameterError(FailedFunctionCall):
  """Function called with an undefined variable."""

  def __init__(self, name):
    super().__init__()
    self.name = name


class DictKeyMissing(Exception, ReturnValueMixin):
  """When retrieving a key that does not exist in a dict."""

  def __init__(self, name):
    super().__init__()
    self.name = name

  def __gt__(self, other):
    return other is None

  def __le__(self, other):
    return not self.__gt__(other)


@dataclasses.dataclass(eq=True, frozen=True)
class BadCall:
  sig: types.Signature
  passed_args: Sequence[tuple[str, types.BaseValue]]
  bad_param: BadType | None


class InvalidParameters(FailedFunctionCall):
  """Exception for functions called with an incorrect parameter combination."""

  def __init__(self, sig, passed_args, ctx, bad_param=None):
    super().__init__()
    self.name = sig.name
    passed_args = [
        (name, ctx.convert.merge_values(arg.data))
        for name, arg, _ in sig.iter_args(passed_args)
    ]
    self.bad_call = BadCall(
        sig=sig, passed_args=passed_args, bad_param=bad_param
    )


class WrongArgTypes(InvalidParameters):
  """For functions that were called with the wrong types."""

  def __init__(self, sig, passed_args, ctx, bad_param):
    if not sig.has_param(bad_param.name):
      sig = sig.insert_varargs_and_kwargs(
          name for name, *_ in sig.iter_args(passed_args)
      )
    super().__init__(sig, passed_args, ctx, bad_param)

  def __gt__(self, other):
    if other is None:
      return True
    if not isinstance(other, WrongArgTypes):
      # WrongArgTypes should take precedence over other FailedFunctionCall
      # subclasses but not over unrelated errors like DictKeyMissing.
      return isinstance(other, FailedFunctionCall)

    # The signature that has fewer *args/**kwargs tends to be more precise.
    def starcount(err):
      return bool(err.bad_call.sig.varargs_name) + bool(
          err.bad_call.sig.kwargs_name
      )

    return starcount(self) < starcount(other)

  def __le__(self, other):
    return not self.__gt__(other)


class WrongArgCount(InvalidParameters):
  """E.g. if a function expecting 4 parameters is called with 3."""


class WrongKeywordArgs(InvalidParameters):
  """E.g. an arg "x" is passed to a function that doesn't have an "x" param."""

  def __init__(self, sig, passed_args, ctx, extra_keywords):
    super().__init__(sig, passed_args, ctx)
    self.extra_keywords = tuple(extra_keywords)


class DuplicateKeyword(InvalidParameters):
  """E.g. an arg "x" is passed to a function as both a posarg and a kwarg."""

  def __init__(self, sig, passed_args, ctx, duplicate):
    super().__init__(sig, passed_args, ctx)
    self.duplicate = duplicate


class MissingParameter(InvalidParameters):
  """E.g. a function requires parameter 'x' but 'x' isn't passed."""

  def __init__(self, sig, passed_args, ctx, missing_parameter):
    super().__init__(sig, passed_args, ctx)
    self.missing_parameter = missing_parameter


# --------------------------------------------------------
# Typed dict errors


class TypedDictKeyMissing(DictKeyMissing):

  def __init__(self, typed_dict: types.BaseValue, key: str | None):
    super().__init__(key)
    self.typed_dict = typed_dict


# --------------------------------------------------------
# Matcher errors


class MatchError(Exception):

  def __init__(self, bad_type: BadType, *args, **kwargs):
    self.bad_type = bad_type
    super().__init__(bad_type, *args, **kwargs)


class NonIterableStrError(Exception):
  """Error for matching `str` against `Iterable[str]`/`Sequence[str]`/etc."""

  def __init__(self, left_type, other_type):
    super().__init__()
    self.left_type = left_type
    self.other_type = other_type


class ProtocolError(Exception):

  def __init__(self, left_type, other_type):
    super().__init__()
    self.left_type = left_type
    self.other_type = other_type


class ProtocolMissingAttributesError(ProtocolError):

  def __init__(self, left_type, other_type, missing):
    super().__init__(left_type, other_type)
    self.missing = missing


class ProtocolTypeError(ProtocolError):

  def __init__(self, left_type, other_type, attribute, actual, expected):
    super().__init__(left_type, other_type)
    self.attribute_name = attribute
    self.actual_type = actual
    self.expected_type = expected


class TypedDictError(Exception):

  def __init__(self, bad, extra, missing):
    super().__init__()
    self.bad = bad
    self.missing = missing
    self.extra = extra


@dataclasses.dataclass
class MatcherErrorDetails:
  protocol: ProtocolError | None = None
  noniterable_str: NonIterableStrError | None = None
  typed_dict: TypedDictError | None = None
