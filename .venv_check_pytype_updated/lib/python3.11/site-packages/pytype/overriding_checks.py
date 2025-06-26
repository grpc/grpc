"""Utilities for checking function overrides used in vm.py."""

from collections.abc import Mapping
import dataclasses
import enum
import logging
from typing import Any

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.pytd import pytd

log = logging.getLogger(__name__)

# This should be context.Context, which can't be imported due to a circular dep.
_ContextType = Any
_SignatureMapType = Mapping[str, function.Signature]


@enum.unique
class SignatureErrorType(enum.Enum):
  """Constants representing various signature mismatch errors."""

  NO_ERROR = enum.auto()
  DEFAULT_PARAMETER_MISMATCH = enum.auto()
  DEFAULT_VALUE_MISMATCH = enum.auto()
  KWONLY_PARAMETER_COUNT_MISMATCH = enum.auto()
  KWONLY_PARAMETER_NAME_MISMATCH = enum.auto()
  KWONLY_PARAMETER_TYPE_MISMATCH = enum.auto()
  POSITIONAL_PARAMETER_COUNT_MISMATCH = enum.auto()
  POSITIONAL_PARAMETER_NAME_MISMATCH = enum.auto()
  POSITIONAL_PARAMETER_TYPE_MISMATCH = enum.auto()
  RETURN_TYPE_MISMATCH = enum.auto()


@dataclasses.dataclass
class SignatureError:
  error_code: SignatureErrorType = SignatureErrorType.NO_ERROR
  message: str = ""


def _get_varargs_annotation_type(param_type):
  """Returns the type annotation for the varargs parameter."""
  # If varargs are annotated with type T in the function definition,
  # the annotation in the signature will be Tuple[T, ...].
  if not isinstance(param_type, abstract.ParameterizedClass):
    # Annotation in the signature could be just 'tuple' that corresponds to
    # varargs with no annotation.
    return None
  return param_type.get_formal_type_parameter(abstract_utils.T)


def _check_positional_parameter_annotations(
    method_signature, base_signature, is_subtype
):
  """Checks type annotations for positional parameters of the overriding method.

  Args:
    method_signature: signature of the overriding method.
    base_signature: signature of the overridden method.
    is_subtype: a binary function to compare types.

  Returns:
    SignatureError if a mismatch is detected. Otherwise returns None.
  """
  for param_index in range(
      max(len(base_signature.param_names), len(method_signature.param_names))
  ):
    if param_index == 0:
      # No type checks for 'self' parameter.
      continue
    if param_index < len(base_signature.param_names):
      base_param_name = base_signature.param_names[param_index]
    elif base_signature.varargs_name:
      base_param_name = base_signature.varargs_name
    else:
      # Remaining positional parameters aren't mapped. Not checking annotations.
      break

    try:
      base_param_type = base_signature.annotations[base_param_name]
    except KeyError:
      # Parameter not annotated in overridden method.
      continue

    if base_param_name == base_signature.varargs_name:
      base_param_type = _get_varargs_annotation_type(base_param_type)
      if base_param_type is None:
        continue

    if param_index < method_signature.posonly_count:
      # Positional-only parameters should match by position.
      method_param_name = method_signature.param_names[param_index]
    elif param_index < len(method_signature.param_names):
      if (
          base_param_name == "_"
          or method_signature.param_names[param_index] == "_"
      ):
        # Underscore parameters should match by position.
        method_param_name = method_signature.param_names[param_index]
      else:
        # Other positional-or-keyword parameters should match by name.
        method_param_name = base_param_name
    elif method_signature.varargs_name:
      method_param_name = method_signature.varargs_name
    else:
      # Remaining positional parameters aren't mapped. Not checking annotations.
      break

    try:
      method_param_type = method_signature.annotations[method_param_name]
    except KeyError:
      # Parameter not annotated in overriding method.
      continue

    if method_param_name == method_signature.varargs_name:
      method_param_type = _get_varargs_annotation_type(method_param_type)
      if method_param_type is None:
        continue

    # Parameter type of the overridden method must be a subtype of the
    # parameter type of the overriding method.
    if not is_subtype(base_param_type, method_param_type):
      return SignatureError(
          SignatureErrorType.POSITIONAL_PARAMETER_TYPE_MISMATCH,
          f"Type mismatch for parameter '{method_param_name}'.",
      )

  return None


def _check_positional_parameters(
    method_signature, base_signature, is_subtype, ctx
):
  """Checks that the positional parameters of the overriding method match.

  Args:
    method_signature: signature of the overriding method.
    base_signature: signature of the overridden method.
    is_subtype: a binary function to compare types.
    ctx: Context

  Returns:
    SignatureError if a mismatch is detected. Otherwise returns None.
  """
  check_types = True
  # Check mappings of positional-or-keyword parameters of the overridden method.
  for base_param_pos, base_param_name in enumerate(base_signature.param_names):
    # Skip positional-only parameters - those that cannot be passed by name.
    if base_param_pos == 0 or base_param_pos < base_signature.posonly_count:
      continue
    if base_param_name == "_":
      continue

    # Positional-or-keyword cannot map to positional-only.
    if base_param_pos < method_signature.posonly_count:
      return SignatureError(
          SignatureErrorType.POSITIONAL_PARAMETER_COUNT_MISMATCH,
          "Too many positional-only parameters in overriding method.",
      )
    elif base_param_pos < len(method_signature.param_names):
      method_param_name = method_signature.param_names[base_param_pos]
    else:
      # Positional-or-keyword cannot map to keyword-only.
      if method_signature.varargs_name:
        # Remaining parameters map to the varargs parameters.
        break
      return SignatureError(
          SignatureErrorType.POSITIONAL_PARAMETER_COUNT_MISMATCH,
          "Not enough positional parameters in overriding method.",
      )

    # Positional-or-keyword parameters must have the same name or underscore.
    method_param_name = method_signature.param_names[base_param_pos]
    if method_param_name not in (base_param_name, "_"):
      # We don't report it as an error, as this is a very common practice
      # in the absence of positional-only parameters.
      # TODO(sinopalnikov): clean it up and start flagging the error.
      log.warning("Name mismatch for parameter %r.", base_param_name)
      # We match positional parameter type annotations by name, not position,
      # later on, so if we have a name mismatch here we should disable
      # annotation checking and just check param count.
      if not ctx.options.overriding_renamed_parameter_count_checks:
        return None
      check_types = False
  # Check mappings of remaining positional parameters of the overriding method
  # that don't map to any positional parameters of the overridden method.
  remaining_method_params = (
      method_signature.param_names[len(base_signature.param_names) :]
      if not base_signature.varargs_name
      else []
  )
  for method_param_name in remaining_method_params:

    # Keyword-only can map to remaining positional.
    if method_param_name in base_signature.kwonly_params:
      continue

    # Otherwise remaining positional must have a default value.
    if method_param_name not in method_signature.defaults:
      return SignatureError(
          SignatureErrorType.DEFAULT_PARAMETER_MISMATCH,
          f"Parameter '{method_param_name}' must have a default value.",
      )

  if not check_types:
    return None
  return _check_positional_parameter_annotations(
      method_signature, base_signature, is_subtype
  )


def _check_keyword_only_parameters(
    method_signature, base_signature, is_subtype
):
  """Checks that the keyword-only parameters of the overriding method match.

  Args:
    method_signature: signature of the overriding method.
    base_signature: signature of the overridden method.
    is_subtype: a binary function to compare types.

  Returns:
    SignatureError if a mismatch is detected. Otherwise returns None.
  """

  base_kwonly_params = set(base_signature.kwonly_params)
  method_kwonly_params = set(method_signature.kwonly_params)
  method_defaults = set(method_signature.defaults)

  if not base_signature.kwargs_name:
    # Keyword-only parameters of the overriding method that don't match any
    # keyword-only parameter of the overridden method must have a default value.
    for method_param_name in method_kwonly_params.difference(
        base_kwonly_params
    ).difference(method_defaults):
      return SignatureError(
          SignatureErrorType.DEFAULT_PARAMETER_MISMATCH,
          f"Parameter '{method_param_name}' must have a default value.",
      )

  # A keyword-only parameter of the overridden method cannot have the same name
  # as a positional-only parameter of the overriding method.
  for base_param_name in base_kwonly_params.difference(method_kwonly_params):
    try:
      method_param_index = method_signature.param_names.index(base_param_name)
    except ValueError:
      if not method_signature.kwargs_name:
        return SignatureError(
            SignatureErrorType.KWONLY_PARAMETER_NAME_MISMATCH,
            f"Parameter '{base_param_name}' not found in overriding method.",
        )
    else:
      if method_param_index < method_signature.posonly_count:
        return SignatureError(
            SignatureErrorType.KWONLY_PARAMETER_NAME_MISMATCH,
            (
                f"Keyword-only parameter '{base_param_name}' of the overridden "
                "method has the same name as a positional-only parameter"
                "of the overriding method."
            ),
        )

  # Check annotations of keyword-only parameters.
  for base_param_name in base_signature.kwonly_params:
    try:
      base_param_type = base_signature.annotations[base_param_name]
    except KeyError:
      # Parameter not annotated in the overridden method.
      continue

    if (
        base_param_name in method_kwonly_params
        or base_param_name in method_signature.param_names
    ):
      method_param_name = base_param_name
    elif method_signature.kwargs_name:
      method_param_name = method_signature.kwargs_name
    else:
      continue

    try:
      method_param_type = method_signature.annotations[method_param_name]
    except KeyError:
      # Parameter not annotated in the overriding method.
      continue

    if method_param_name == method_signature.kwargs_name:
      if isinstance(method_param_type, abstract.ParameterizedClass):
        # If kwargs are annotated with type T in the function definition,
        # the annotation in the signature will be Dict[str, T].
        method_param_type = method_param_type.get_formal_type_parameter(
            abstract_utils.V
        )
      else:
        # If the kwargs type is a plain dict, there's nothing to check.
        continue

    # Parameter type of the overridden method must be a subtype of the
    # parameter type of the overriding method.
    if not is_subtype(base_param_type, method_param_type):
      return SignatureError(
          SignatureErrorType.KWONLY_PARAMETER_TYPE_MISMATCH,
          f"Type mismatch for parameter '{base_param_name}'.",
      )

  return None


def _check_default_values(method_signature, base_signature):
  """Checks that default parameter values of the overriding method match.

  Args:
    method_signature: signature of the overriding method.
    base_signature: signature of the overridden method.

  Returns:
    SignatureError if a mismatch is detected. Otherwise returns None.
  """

  for base_param_name, base_default_value in base_signature.defaults.items():
    if base_param_name in base_signature.kwonly_params:
      # The parameter is keyword-only, check match by name.
      if (
          base_param_name not in method_signature.kwonly_params
          and base_param_name not in method_signature.param_names
      ):
        continue
      method_param_name = base_param_name
    else:
      # The parameter is positional, check match by position.
      base_param_index = base_signature.param_names.index(base_param_name)
      if base_param_index >= len(method_signature.param_names):
        continue
      method_param_name = method_signature.param_names[base_param_index]

    try:
      method_default_value = method_signature.defaults[method_param_name]
    except KeyError:
      return SignatureError(
          SignatureErrorType.DEFAULT_PARAMETER_MISMATCH,
          f"Parameter '{method_param_name}' must have a default value.",
      )

    # Only concrete values can be compared for an exact match.
    try:
      base_default = abstract_utils.get_atomic_python_constant(
          base_default_value
      )
      method_default = abstract_utils.get_atomic_python_constant(
          method_default_value
      )
    except abstract_utils.ConversionError:
      continue

    if base_default != method_default:
      return SignatureError(
          SignatureErrorType.DEFAULT_VALUE_MISMATCH,
          f"Parameter '{base_param_name}' must have the same default value.",
      )

  return None


def _check_return_types(method_signature, base_signature, is_subtype):
  """Checks that the return types match."""
  try:
    base_return_type = base_signature.annotations["return"]
    method_return_type = method_signature.annotations["return"]
  except KeyError:
    # Return type not annotated in either of the two methods.
    return None

  if isinstance(base_return_type, abstract.AMBIGUOUS_OR_EMPTY) or isinstance(
      method_return_type, abstract.AMBIGUOUS_OR_EMPTY
  ):
    return None

  # Return type of the overriding method must be a subtype of the
  # return type of the overridden method.
  if not is_subtype(method_return_type, base_return_type):
    return SignatureError(
        SignatureErrorType.RETURN_TYPE_MISMATCH, "Return type mismatch."
    )

  return None


def _check_signature_compatible(
    method_signature, base_signature, stack, matcher, ctx
):
  """Checks if the signatures match for the overridden and overriding methods.

  Adds the first error found to the context's error log.

  Two invariants are verified:
  1. Every call that is valid for the overridden method is valid for
     the overriding method.
  2. Two calls that are equivalent for the overridden method are equivalent
     for the overriding method.

  This translates into the following mapping requirements for
  overriding method parameters:
  +----------------------------------------------------------------------------+
  | Overridden method                    | Overriding method                   |
  +----------------------------------------------------------------------------+
  | Positional-only                      | Positional-only or                  |
  |                                      | positional-or-keyword, any name     |
  +--------------------------------------+-------------------------------------+
  | Positional-or-keyword                | Positional-or-keyword, same name    |
  +--------------------------------------+-------------------------------------+
  | Keyword-only                         | Positional-or-keyword               |
  |                                      | or keyword-only, same name          |
  +--------------------------------------+-------------------------------------+
  | Non-default                          | Non-default or default              |
  +--------------------------------------+-------------------------------------+
  | Default                              | Default, same default value         |
  +--------------------------------------+-------------------------------------+
  | Parameter of type T                  | Parameter of supertype of T or      |
  |                                      | no annotation                       |
  +--------------------------------------+-------------------------------------+
  | Parameter without annotation         | Parameter with any type annotation  |
  |                                      | or without annotation               |
  +--------------------------------------+-------------------------------------+
  | Return type T                        | Return type - subtype of T or       |
  |                                      | no annotation                       |
  +--------------------------------------+-------------------------------------+
  | Return type not annotated            | Any return type annotation          |
  |                                      | or no annotation                    |
  +--------------------------------------+-------------------------------------+
  In addition, default parameters of the overriding method don't have to match
  any parameters of the overridden method.
  Same name requirement is often violated, so we don't treat is as an error
  for now and only log a warning.

  Arguments:
    method_signature: signature of the overriding method.
    base_signature: signature of the overridden method.
    stack: the stack to use for mismatch error reporting.
    matcher: abstract matcher for type comparison.
    ctx: pytype abstract context.
  """

  def is_subtype(this_type, that_type):
    """Return True iff this_type is a subclass of that_type."""
    if this_type == ctx.convert.never:
      return True  # Never is the bottom type, so it matches everything
    this_type_instance = this_type.instantiate(
        ctx.root_node, container=abstract_utils.DUMMY_CONTAINER
    )
    return matcher.compute_one_match(this_type_instance, that_type).success

  check_result = (
      _check_positional_parameters(
          method_signature, base_signature, is_subtype, ctx
      )
      or _check_keyword_only_parameters(
          method_signature, base_signature, is_subtype
      )
      or _check_default_values(method_signature, base_signature)
      or _check_return_types(method_signature, base_signature, is_subtype)
  )

  if check_result:
    ctx.errorlog.overriding_signature_mismatch(
        stack, base_signature, method_signature, details=check_result.message
    )


def _get_pytd_class_signature_map(
    cls: abstract.PyTDClass, ctx: _ContextType
) -> _SignatureMapType:
  """Returns a map from method names to their signatures for a PyTDClass."""
  if cls in ctx.method_signature_map:
    return ctx.method_signature_map[cls]

  method_signature_map = {}
  pytd_cls = cls.pytd_cls
  for pytd_func in pytd_cls.methods:
    if pytd_func.kind != pytd.MethodKind.METHOD:
      continue
    func_name = pytd_func.name
    if func_name in ("__new__", "__init__"):
      continue
    # Assume every method has at least one signature.
    # Ignore overloaded methods, take only the first signature.
    pytd_signature = pytd_func.signatures[0]
    signature = function.Signature.from_pytd(ctx, func_name, pytd_signature)
    assert func_name not in method_signature_map
    method_signature_map[func_name] = signature

  ctx.method_signature_map[cls] = method_signature_map
  return method_signature_map


def _get_parameterized_class_signature_map(
    cls: abstract.ParameterizedClass, ctx: _ContextType
) -> _SignatureMapType:
  """Returns a map from method names to signatures for a ParameterizedClass."""
  if cls in ctx.method_signature_map:
    return ctx.method_signature_map[cls]

  base_class = cls.base_cls

  if isinstance(base_class, abstract.InterpreterClass):
    base_signature_map = ctx.method_signature_map[base_class]
  else:
    assert isinstance(base_class, abstract.PyTDClass)
    base_signature_map = _get_pytd_class_signature_map(base_class, ctx)

  method_signature_map = {}
  for base_method_name, base_method_signature in base_signature_map.items():
    # Replace formal type parameters with their values.
    annotations = ctx.annotation_utils.sub_annotations_for_parameterized_class(
        cls, base_method_signature.annotations
    )
    method_signature_map[base_method_name] = base_method_signature._replace(
        annotations=annotations
    )

  ctx.method_signature_map[cls] = method_signature_map
  return method_signature_map


def check_overriding_members(cls, bases, members, matcher, ctx):
  """Check that the method signatures of the new class match base classes."""

  # Maps method names to methods.
  class_method_map = {}
  for member_name, member_value in members.items():
    try:
      atomic_value = abstract_utils.get_atomic_value(
          member_value, constant_type=abstract.InterpreterFunction
      )
    except abstract_utils.ConversionError:
      continue
    method = atomic_value
    if method.is_classmethod:
      continue
    method_name = method.name.rsplit(".")[-1]
    if method_name in ("__new__", "__init__"):
      continue
    assert member_name not in class_method_map
    class_method_map[member_name] = method

  class_signature_map = {}
  for method_name, method in class_method_map.items():
    if method.is_unannotated_coroutine():
      annotations = dict(method.signature.annotations)
      annotations["return"] = ctx.convert.coroutine_type
      signature = method.signature._replace(annotations=annotations)
    else:
      signature = method.signature
    class_signature_map[method_name] = signature

  base_classes = []
  for base in bases:
    try:
      base_class = abstract_utils.get_atomic_value(base)
    except abstract_utils.ConversionError:
      continue
    base_classes.append(base_class)
  mro = [mro_class.full_name for mro_class in cls.mro]

  for i, base_class in enumerate(base_classes):
    if isinstance(base_class, abstract.InterpreterClass):
      base_signature_map = ctx.method_signature_map[base_class]
    elif isinstance(base_class, abstract.ParameterizedClass):
      base_signature_map = _get_parameterized_class_signature_map(
          base_class, ctx
      )
    elif isinstance(base_class, abstract.PyTDClass):
      base_signature_map = _get_pytd_class_signature_map(base_class, ctx)
    else:
      continue

    for base_method_name, base_method_signature in base_signature_map.items():
      if base_method_name not in class_signature_map:
        continue
      class_method_signature = class_signature_map[base_method_name]
      # If the method is defined in the current class then report mismatch
      # errors on the method definition, otherwise report them on the current
      # class definition.
      method_def_opcode = (
          class_method_map[base_method_name].def_opcode
          if base_method_name in class_method_map
          else None
      )
      stack = ctx.vm.simple_stack(method_def_opcode)
      _check_signature_compatible(
          class_method_signature, base_method_signature, stack, matcher, ctx
      )

    # We filter out any methods inherited from a base class that comes after the
    # next direct base class in the MRO, to avoid checking signature
    # compatibility in the wrong direction.
    if i < len(base_classes) - 1:
      next_index = mro.index(base_classes[i + 1].full_name)
      filtered_base_map = {}
      for base_method_name, base_method_signature in base_signature_map.items():
        defining_class = _get_defining_class(base_method_signature)
        if defining_class:
          try:
            defining_index = mro.index(defining_class)
          except ValueError:
            include = True
          else:
            include = defining_index < next_index
        else:
          include = True
        if include:
          filtered_base_map[base_method_name] = base_method_signature
    else:
      filtered_base_map = base_signature_map
    # Methods defined in this class take precedence.
    class_signature_map = {**filtered_base_map, **class_signature_map}

  assert cls not in ctx.method_signature_map
  ctx.method_signature_map[cls] = class_signature_map


def _get_defining_class(sig: function.Signature) -> str | None:
  if "self" in sig.annotations:
    return sig.annotations["self"].full_name
  elif "." in sig.name:
    return sig.name.rsplit(".", 1)[0]
  else:
    return None
