"""A printer for human-readable output of error messages."""

import collections
import dataclasses
import enum

from pytype import matcher
from pytype import pretty_printer_base
from pytype.errors import error_types
from pytype.pytd import pytd_utils
from pytype.pytd import slots
from pytype.typegraph import cfg
from pytype.types import types


@dataclasses.dataclass
class BadReturn:
  expected: str
  bad_actual: str
  full_actual: str
  error_details: list[str]


@dataclasses.dataclass
class BadCall:
  expected: str
  actual: str
  error_details: list[str]


class BadAttrType(enum.Enum):
  OBJECT = 0
  SYMBOL = 1
  MODULE = 2


@dataclasses.dataclass
class BadAttr:
  obj: str
  obj_type: BadAttrType


class BadCallPrinter:
  """Print the details of a BadCall."""

  def __init__(
      self,
      pp: pretty_printer_base.PrettyPrinterBase,
      bad_call: error_types.BadCall,
  ):
    self.bad_call = bad_call
    self._pp = pp

  def _iter_sig(self):
    """Iterate through a Signature object. Focus on a bad parameter."""
    sig = self.bad_call.sig
    for name in sig.posonly_params:
      yield "", name
    if sig.posonly_params:
      yield ("/", "")
    for name in sig.param_names[sig.posonly_count :]:
      yield "", name
    if sig.varargs_name is not None:
      yield "*", sig.varargs_name
    elif sig.kwonly_params:
      yield ("*", "")
    for name in sorted(sig.kwonly_params):
      yield "", name
    if sig.kwargs_name is not None:
      yield "**", sig.kwargs_name

  def _iter_expected(self):
    """Yield the prefix, name and type information for expected parameters."""
    bad_param = self.bad_call.bad_param
    sig = self.bad_call.sig
    for prefix, name in self._iter_sig():
      suffix = " = ..." if sig.has_default(name) else ""
      if bad_param and name == bad_param.name:
        type_str = self._pp.print_type_of_instance(bad_param.typ)
        suffix = ": " + type_str + suffix
      yield prefix, name, suffix

  def _iter_actual(self, literal):
    """Yield the prefix, name and type information for actual parameters."""
    # We want to display the passed_args in the order they're defined in the
    # signature, unless there are starargs or starstarargs.
    # Map param names to their position in the list, then sort the list of
    # passed args so it's in the same order as the params.
    sig = self.bad_call.sig
    passed_args = self.bad_call.passed_args
    bad_param = self.bad_call.bad_param
    keys = {param: n for n, (_, param) in enumerate(self._iter_sig())}

    def key_f(arg):
      arg_name = arg[0]
      # starargs are given anonymous names, which won't be found in the sig.
      # Instead, use the same name as the varags param itself, if present.
      if arg_name not in keys and pytd_utils.ANON_PARAM.match(arg_name):
        return keys.get(sig.varargs_name, len(keys) + 1)
      return keys.get(arg_name, len(keys) + 1)

    for name, arg in sorted(passed_args, key=key_f):
      if bad_param and name == bad_param.name:
        suffix = ": " + self._pp.print_type(arg, literal=literal)
      else:
        suffix = ""
      yield "", name, suffix

  def _print_args(self, arg_iter):
    """Pretty-print a list of arguments. Focus on a bad parameter."""
    # (foo, bar, broken : type, ...)
    bad_param = self.bad_call.bad_param
    printed_params = []
    found = False
    for prefix, name, suffix in arg_iter:
      if bad_param and name == bad_param.name:
        printed_params.append(prefix + name + suffix)
        found = True
      elif found:
        printed_params.append("...")
        break
      elif pytd_utils.ANON_PARAM.match(name):
        printed_params.append(prefix + "_")
      else:
        printed_params.append(prefix + name)
    return ", ".join(printed_params)

  def print_call_details(self):
    bad_param = self.bad_call.bad_param
    expected = self._print_args(self._iter_expected())
    literal = "Literal[" in expected
    actual = self._print_args(self._iter_actual(literal))
    if bad_param and bad_param.error_details:
      mp = MatcherErrorPrinter(self._pp)
      details = mp.print_error_details(bad_param.error_details)
    else:
      details = []
    return BadCall(expected, actual, details)


class MatcherErrorPrinter:
  """Pretty printer for some specific matcher error types."""

  def __init__(self, pp: pretty_printer_base.PrettyPrinterBase):
    self._pp = pp

  def _print_protocol_error(self, error: error_types.ProtocolError) -> str:
    """Pretty-print the protocol error."""
    convert = self._pp.ctx.pytd_convert
    with convert.set_output_mode(convert.OutputMode.DETAILED):
      left = self._pp.print_pytd(error.left_type.to_pytd_type_of_instance())
      protocol = self._pp.print_pytd(
          error.other_type.to_pytd_type_of_instance()
      )
    if isinstance(error, error_types.ProtocolMissingAttributesError):
      missing = ", ".join(sorted(error.missing))
      return (
          f"Attributes of protocol {protocol} are not implemented on "
          f"{left}: {missing}"
      )
    else:
      assert isinstance(error, error_types.ProtocolTypeError)
      actual, expected = error.actual_type, error.expected_type
      if isinstance(actual, types.Function) and isinstance(
          expected, types.Function
      ):
        # TODO(b/196434939): When matching a protocol like Sequence[int] the
        # protocol name will be Sequence[int] but the method signatures will be
        # displayed as f(self: Sequence[_T], ...).
        actual = self._pp.print_function_def(actual)
        expected = self._pp.print_function_def(expected)
        return (
            f"\nMethod {error.attribute_name} of protocol {protocol} has "
            f"the wrong signature in {left}:\n\n"
            f">> {protocol} expects:\n{expected}\n\n"
            f">> {left} defines:\n{actual}"
        )
      else:
        with convert.set_output_mode(convert.OutputMode.DETAILED):
          actual = self._pp.print_pytd(error.actual_type.to_pytd_type())
          expected = self._pp.print_pytd(error.expected_type.to_pytd_type())
        return (
            f"Attribute {error.attribute_name} of protocol {protocol} has "
            f"wrong type in {left}: expected {expected}, got {actual}"
        )

  def _print_noniterable_str_error(self, error) -> str:
    """Pretty-print the matcher.NonIterableStrError instance."""
    return (
        f"Note: {error.left_type.name} does not match string iterables by "
        "default. Learn more: https://github.com/google/pytype/blob/main/docs/faq.md#why-doesnt-str-match-against-string-iterables"
    )

  def _print_typed_dict_error(self, error) -> str:
    """Pretty-print the TypedDictError instance."""
    ret = ""
    if error.missing:
      ret += "\nTypedDict missing keys: " + ", ".join(error.missing)
    if error.extra:
      ret += "\nTypedDict extra keys: " + ", ".join(error.extra)
    if error.bad:
      ret += "\nTypedDict type errors: "
      for k, bad in error.bad:
        for match in bad:
          actual = self._pp.print_type(match.actual_binding.data)
          expected = self._pp.print_type_of_instance(match.expected.typ)
          ret += f"\n  {{'{k}': ...}}: expected {expected}, got {actual}"
    return ret

  def print_error_details(
      self, details: error_types.MatcherErrorDetails
  ) -> list[str]:
    errors: list[str] = []
    if details.protocol:
      errors.append(self._print_protocol_error(details.protocol))
    if details.noniterable_str:
      errors.append(self._print_noniterable_str_error(details.noniterable_str))
    if details.typed_dict:
      errors.append(self._print_typed_dict_error(details.typed_dict))
    return ["\n" + err for err in errors]

  def prepare_errorlog_details(self, bad: list[matcher.BadMatch]) -> list[str]:
    """Prepare printable annotation matching errors."""
    details = collections.defaultdict(set)
    for match in bad:
      d = self.print_error_details(match.error_details)
      for i, detail in enumerate(d):
        if detail:
          details[i].add(detail)
    ret = []
    for i in sorted(details.keys()):
      ret.extend(sorted(details[i]))
    return ret

  def print_return_types(
      self, node: cfg.CFGNode, bad: list[matcher.BadMatch]
  ) -> BadReturn:
    """Print the actual and expected values for a return type."""
    formal = bad[0].expected.typ
    convert = self._pp.ctx.pytd_convert
    with convert.set_output_mode(convert.OutputMode.DETAILED):
      expected = self._pp.print_pytd(formal.to_pytd_type_of_instance(node))
    if "Literal[" in expected:
      output_mode = convert.OutputMode.LITERAL
    else:
      output_mode = convert.OutputMode.DETAILED
    with convert.set_output_mode(output_mode):
      bad_actual = self._pp.print_pytd(
          pytd_utils.JoinTypes(
              match.actual_binding.data.to_pytd_type(node, view=match.view)
              for match in bad
          )
      )
      actual = bad[0].actual
      if len(actual.bindings) > len(bad):
        full_actual = self._pp.print_pytd(
            pytd_utils.JoinTypes(v.to_pytd_type(node) for v in actual.data)
        )
      else:
        full_actual = bad_actual
    # typing.Never is a prettier alias for nothing.
    fmt = lambda ret: "Never" if ret == "nothing" else ret
    error_details = self.prepare_errorlog_details(bad)
    return BadReturn(
        fmt(expected), fmt(bad_actual), fmt(full_actual), error_details
    )


class AttributeErrorPrinter:
  """Pretty printer for attribute errors."""

  def __init__(self, pp: pretty_printer_base.PrettyPrinterBase):
    self._pp = pp

  def print_receiver(self, obj: types.BaseValue, attr_name: str):
    if attr_name in slots.SYMBOL_MAPPING:
      obj_repr = self._pp.print_type(obj)
      return BadAttr(obj_repr, BadAttrType.SYMBOL)
    elif isinstance(obj, types.Module):
      return BadAttr(obj.name, BadAttrType.MODULE)
    else:
      obj_repr = self._pp.print_type(obj)
      return BadAttr(obj_repr, BadAttrType.OBJECT)
