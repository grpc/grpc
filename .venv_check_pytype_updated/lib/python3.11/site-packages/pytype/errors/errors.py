"""Code and data structures for storing and displaying errors."""

from collections.abc import Callable, Iterable, Sequence
import contextlib
import csv
import io
import logging
import sys
from typing import IO, TypeVar

from pytype import debug
from pytype import pretty_printer_base
from pytype import utils
from pytype.errors import error_printer
from pytype.errors import error_types
from pytype.pytd import slots
from pytype.types import types

# Usually we call the logger "log" but that name is used quite often here.
_log = logging.getLogger(__name__)


# "Error level" enum for distinguishing between warnings and errors:
SEVERITY_WARNING = 1
SEVERITY_ERROR = 2

# The set of known error names.
_ERROR_NAMES = set()

# The current error name, managed by the error_name decorator.
_CURRENT_ERROR_NAME = utils.DynamicVar()

# Max number of calls in the traceback string.
MAX_TRACEBACK_LENGTH = 3

# Max number of tracebacks to show for the same error.
MAX_TRACEBACKS = 3

# Marker indicating the start of a traceback.
TRACEBACK_MARKER = "Called from (traceback):"

# Symbol representing an elided portion of the stack.
_ELLIPSIS = object()

_FuncT = TypeVar("_FuncT", bound=Callable)

_ERROR_RED_HIGHLIGHTED = utils.COLOR_ERROR_NAME_TEMPLATE % "error"


def _error_name(name) -> Callable[[_FuncT], _FuncT]:
  """Decorate a function so that it binds the current error name."""
  _ERROR_NAMES.add(name)

  def wrap(func):
    def invoke(*args, **kwargs):
      with _CURRENT_ERROR_NAME.bind(name):
        return func(*args, **kwargs)

    return invoke

  return wrap


def _maybe_truncate_traceback(traceback):
  """Truncate the traceback if it is too long.

  Args:
    traceback: A list representing an error's traceback. There should be one
      list item per entry in the traceback (in the right order); beyond that,
      this function does not care about the item types.

  Returns:
    The traceback, possibly with some items removed and an _ELLIPSIS inserted.
    Guaranteed to be no longer than MAX_TRACEBACK_LENGTH.
  """
  if len(traceback) > MAX_TRACEBACK_LENGTH:
    return traceback[: MAX_TRACEBACK_LENGTH - 2] + [_ELLIPSIS, traceback[-1]]
  else:
    return traceback


def _make_traceback_str(frames):
  """Turn a stack of frames into a traceback string."""
  if len(frames) < 2 or (
      frames[-1].f_code and not frames[-1].f_code.get_arg_count()
  ):
    # A traceback is usually unnecessary when the topmost frame has no
    # arguments. If this frame ran during module loading, caching prevented it
    # from running again without a traceback, so we drop the traceback manually.
    return None
  frames = frames[:-1]
  frames = _maybe_truncate_traceback(frames)
  traceback = []
  format_line = "line %d, in %s"
  for frame in frames:
    if frame is _ELLIPSIS:
      line = "..."
    elif frame.current_opcode.code.name == "<module>":
      line = format_line % (frame.current_opcode.line, "current file")
    else:
      line = format_line % (
          frame.current_opcode.line,
          frame.current_opcode.code.name,
      )
    traceback.append(line)
  return TRACEBACK_MARKER + "\n  " + "\n  ".join(traceback)


def _dedup_opcodes(stack):
  """Dedup the opcodes in a stack of frames."""
  deduped_stack = []
  if len(stack) > 1:
    stack = [x for x in stack if not x.skip_in_tracebacks]
  for frame in stack:
    if frame.current_opcode:
      if deduped_stack and (
          frame.current_opcode.line == deduped_stack[-1].current_opcode.line
      ):
        continue
      # We can have consecutive opcodes with the same line number due to, e.g.,
      # a set comprehension. The first opcode we encounter is the one with the
      # real method name, whereas the second's method name is something like
      # <setcomp>, so we keep the first.
      deduped_stack.append(frame)
  return deduped_stack


def _compare_traceback_strings(left, right):
  """Try to compare two traceback strings.

  Two traceback strings are comparable if they are equal, or if one ends with
  the other. For example, these two tracebacks are comparable:
    Traceback:
      line 1, in <module>
      line 2, in foo
    Traceback:
      line 2, in foo
  and the first is greater than the second.

  Args:
    left: A string or None.
    right: A string or None.

  Returns:
    None if the inputs aren't comparable, else an integer.
  """
  if left == right:
    return 0
  left = left[len(TRACEBACK_MARKER) :] if left else ""
  right = right[len(TRACEBACK_MARKER) :] if right else ""
  if left.endswith(right):
    return 1
  elif right.endswith(left):
    return -1
  else:
    return None


def _function_name(name, capitalize=False):
  builtin_prefix = "builtins."
  if name.startswith(builtin_prefix):
    ret = f"built-in function {name[len(builtin_prefix):]}"
  else:
    ret = f"function {name}"
  if capitalize:
    return ret[0].upper() + ret[1:]
  else:
    return ret


class CheckPoint:
  """Represents a position in an error log."""

  def __init__(self, errors):
    self._errorlog_errors = errors
    self._position = len(errors)
    self.errors = None

  def revert(self):
    self.errors = self._errorlog_errors[self._position :]
    self._errorlog_errors[:] = self._errorlog_errors[: self._position]


class Error:
  """Representation of an error in the error log.

  Attributes:
    name: The error name.
    bad_call: Optionally, a `pytype.function.BadCall` of details of a bad
      function call.
    details: Optionally, a string of message details.
    filename: The file in which the error occurred.
    line: The line number at which the error occurred.
    message: The error message string.
    methodname: The method in which the error occurred.
    severity: The error level (error or warning), an integer.
    keyword: Optionally, the culprit keyword in the line where error is.
      e.g.,
      message = "No attribute '_submatch' on BasePattern"
      keyword = _submatch
    keyword_context: Optionally, a string naming the object on which `keyword`
      occurs. e.g. the fully qualified module name that a non-existent function
      doesn't exist on.
    traceback: Optionally, an error traceback.
    opcode_name: Optionally, the name of the opcode that raised the error.
  """

  def __init__(
      self,
      severity,
      message,
      filename=None,
      line=0,
      endline=0,
      col=0,
      endcol=0,
      src="",
      methodname=None,
      details=None,
      traceback=None,
      keyword=None,
      keyword_context=None,
      bad_call=None,
      opcode_name=None,
  ):
    name = _CURRENT_ERROR_NAME.get()
    assert (
        name
    ), "Errors must be created from a caller annotated with @error_name."
    # Required for every Error.
    self._severity = severity
    self._message = message
    self._name = name
    self._src = src
    # Optional information about the error.
    self._details = details
    # Optional information about error position.
    self._filename = filename
    self._line = line or 0
    self._endline = endline or 0
    self._col = col or 0
    self._endcol = endcol or 0
    self._methodname = methodname
    self._traceback = traceback
    self._keyword_context = keyword_context
    self._keyword = keyword
    self._bad_call = bad_call
    self._opcode_name = opcode_name

  @classmethod
  def with_stack(cls, stack, severity, message, **kwargs):
    """Return an error using a stack for position information.

    Args:
      stack: A list of state.Frame or state.SimpleFrame objects.
      severity: The error level (error or warning), an integer.
      message: The error message string.
      **kwargs: Additional keyword args to pass onto the class ctor.

    Returns:
      An Error object.
    """
    stack = _dedup_opcodes(stack) if stack else None
    opcode = stack[-1].current_opcode if stack else None
    if opcode is None:
      return cls(severity, message, **kwargs)
    else:
      return cls(
          severity,
          message,
          filename=opcode.code.filename,
          line=opcode.line,
          endline=opcode.endline,
          col=opcode.col,
          endcol=opcode.endcol,
          methodname=opcode.code.name,
          opcode_name=opcode.__class__.__name__,
          traceback=_make_traceback_str(stack),
          **kwargs,
      )

  @classmethod
  def for_test(cls, severity, message, name, **kwargs):
    """Create an _Error with the specified name, for use in tests."""
    with _CURRENT_ERROR_NAME.bind(name):
      return cls(severity, message, **kwargs)

  @property
  def name(self):
    return self._name

  @property
  def line(self):
    return self._line

  @property
  def filename(self):
    return self._filename

  @property
  def opcode_name(self):
    return self._opcode_name

  @property
  def message(self):
    message = self._message
    if self._details:
      message += "\n" + self._details
    if self._traceback:
      message += "\n" + self._traceback
    return message

  @property
  def traceback(self):
    return self._traceback

  @property
  def methodname(self):
    return self._methodname

  @property
  def bad_call(self):
    return self._bad_call

  @property
  def details(self):
    return self._details

  @property
  def keyword(self):
    return self._keyword

  @property
  def keyword_context(self):
    return self._keyword_context

  def _find_all_line_split(self, begin_line: int, end_line: int) -> list[int]:
    """Finds all index of line boundaries between begin_line and end_line.

    Due to the possibility of the endline of the error message being on the
    last line of the source code, last line will be the last index, and not past
    the last index.

    Args:
      begin_line: The begin line of which we want to find the index of.
      end_line: The end line of which we want to find the index of.

    Returns:
      A list of indicies for the line boundaries.
    """
    curr_line = 0
    curr_idx = 0
    point_idx = []
    while curr_line < begin_line:
      curr_idx = self._src.find("\n", curr_idx) + 1
      curr_line += 1
    point_idx.append(curr_idx)

    while curr_line < end_line:
      curr_idx = self._src.find("\n", curr_idx) + 1
      point_idx.append(curr_idx)
      curr_line += 1
    curr_idx = self._src.find("\n", curr_idx)
    # Last line index will be not past the last line, it'll be the last char
    # of the last line.
    curr_idx = len(self._src) if curr_idx == -1 else curr_idx
    point_idx.append(curr_idx)

    return point_idx

  def _visualize_failed_lines(self) -> str:
    """Visualize the line with the errors, highlighting the exact error loc."""
    if not self._filename or not self._line or not self._src:
      return ""
    point_idx = self._find_all_line_split(self._line - 1, self._endline - 1)
    if self._line == self._endline:
      return (
          self._src[point_idx[0] : point_idx[-1]]
          + "\n"
          + " " * self._col
          + (
              utils.COLOR_ERROR_NAME_TEMPLATE
              % ("~" * (self._endcol - self._col))
          )
      )

    concat_code_with_red_lines = [
        self._src[point_idx[0] : point_idx[1]],
        " " * self._col,
        (
            utils.COLOR_ERROR_NAME_TEMPLATE
            % ("~" * (point_idx[1] - point_idx[0] - self._col - 1))
        ),
        "\n",
    ]
    for i in range(1, len(point_idx) - 2):
      concat_code_with_red_lines.append(
          self._src[point_idx[i] : point_idx[i + 1]]
      )
      concat_code_with_red_lines.append(
          utils.COLOR_ERROR_NAME_TEMPLATE
          % ("~" * (point_idx[i + 1] - point_idx[i] - 1))
      )
      concat_code_with_red_lines.append("\n")
    concat_code_with_red_lines.append(self._src[point_idx[-2] : point_idx[-1]])
    concat_code_with_red_lines.append("\n")
    concat_code_with_red_lines.append(
        utils.COLOR_ERROR_NAME_TEMPLATE % ("~" * self._endcol)
    )
    return "".join(concat_code_with_red_lines)

  def get_unique_representation(self):
    return (self._position(), self._message, self._details, self._name)

  def _position(self):
    """Return human-readable filename + line number."""
    method = f"in {self._methodname}" if self._methodname else ""

    if self._filename:
      return "%s:%d:%d: %s: %s" % (
          self._filename,
          self._line,
          self._col + 1,  # TODO: b/338455486 - pycnite should pass in 1-based
          _ERROR_RED_HIGHLIGHTED,
          method,
      )
    elif self._line:
      return "%d:%d: %s: %s" % (
          self._line,
          self._col + 1,  # TODO: b/338455486 - pycnite should pass in 1-based
          _ERROR_RED_HIGHLIGHTED,
          method,
      )
    else:
      return ""

  def __str__(self):
    return self.as_string()

  def set_line(self, line):
    self._line = line

  def as_string(self, *, color=False):
    """Format the error as a friendly string, optionally with shell coloring."""
    pos = self._position()
    if pos:
      pos += ": "
    if color:
      name = utils.COLOR_ERROR_NAME_TEMPLATE % self._name
    else:
      name = self._name
    text = "{}{} [{}]".format(pos, self._message.replace("\n", "\n  "), name)
    if self._details:
      text += "\n  " + self._details.replace("\n", "\n  ")
    visualized = self._visualize_failed_lines()
    if visualized:
      text += "\n\n" + visualized + "\n"
    if self._traceback:
      text += "\n" + self._traceback
    return text

  def drop_traceback(self):
    with _CURRENT_ERROR_NAME.bind(self._name):
      return self.__class__(
          severity=self._severity,
          message=self._message,
          filename=self._filename,
          line=self._line,
          endline=self._endline,
          col=self._col,
          endcol=self._endcol,
          methodname=self._methodname,
          details=self._details,
          keyword=self._keyword,
          traceback=None,
          src=self._src,
      )


class ErrorLog:
  """A stream of errors."""

  def __init__(self, src: str):
    self._errors = []
    # An error filter (initially None)
    self._filter = None
    self._src = src

  def __len__(self):
    return len(self._errors)

  def __iter__(self):
    return iter(self._errors)

  def __getitem__(self, index):
    return self._errors[index]

  def copy_from(self, errors, stack):
    for e in errors:
      with _CURRENT_ERROR_NAME.bind(e.name):
        self.error(
            stack,
            e._message,
            e.details,
            e.keyword,
            e.bad_call,  # pylint: disable=protected-access
            e.keyword_context,
        )

  def is_valid_error_name(self, name):
    """Return True iff name was defined in an @error_name() decorator."""
    return name in _ERROR_NAMES

  def set_error_filter(self, filt):
    """Set the error filter.

    Args:
      filt: A function or callable object that accepts a single argument of type
        Error and returns True if that error should be included in the log.  A
        filter of None will add all errors.

    NOTE: The filter may adjust some properties of the error.
    """
    self._filter = filt

  def has_error(self):
    """Return true iff an Error with SEVERITY_ERROR is present."""
    # pylint: disable=protected-access
    return any(e._severity == SEVERITY_ERROR for e in self._errors)

  def _add(self, error):
    if self._filter is None or self._filter(error):
      _log.info("Added error to log: %s\n%s", error.name, error)
      if _log.isEnabledFor(logging.DEBUG):
        _log.debug(debug.stack_trace(limit=1).rstrip())
      self._errors.append(error)

  def warn(self, stack, message, *args):
    self._add(
        Error.with_stack(stack, SEVERITY_WARNING, message % args, src=self._src)
    )

  def error(
      self,
      stack,
      message,
      details=None,
      keyword=None,
      bad_call=None,
      keyword_context=None,
      line=None,
  ):
    err = Error.with_stack(
        stack,
        SEVERITY_ERROR,
        message,
        details=details,
        keyword=keyword,
        bad_call=bad_call,
        keyword_context=keyword_context,
        src=self._src,
    )
    if line:
      err.set_line(line)
    self._add(err)

  @contextlib.contextmanager
  def checkpoint(self):
    """Record errors without adding them to the errorlog."""
    _log.info("Checkpointing errorlog at %d errors", len(self._errors))
    checkpoint = CheckPoint(self._errors)
    try:
      yield checkpoint
    finally:
      checkpoint.revert()
    _log.info(
        "Restored errorlog to checkpoint: %d errors reverted",
        len(checkpoint.errors),
    )

  def print_to_csv_file(self, fi: IO[str]):
    """Print the errorlog to a csv file."""
    csv_file = csv.writer(fi, delimiter=",", lineterminator="\n")
    for error in self.unique_sorted_errors():
      # pylint: disable=protected-access
      if error._details and error._traceback:
        details = error._details + "\n\n" + error._traceback
      elif error._traceback:
        details = error._traceback
      else:
        details = error._details
      csv_file.writerow(
          [error._filename, error._line, error._name, error._message, details]
      )

  def print_to_file(self, fi: IO[str], *, color: bool = False):
    for error in self.unique_sorted_errors():
      print(error.as_string(color=color), file=fi)

  def unique_sorted_errors(self):
    """Gets the unique errors in this log, sorted on filename and line."""
    unique_errors = {}
    for error in self._sorted_errors():
      error_without_traceback = error.get_unique_representation()
      if error_without_traceback not in unique_errors:
        unique_errors[error_without_traceback] = [error]
        continue
      errors = unique_errors[error_without_traceback]
      for previous_error in list(errors):  # make a copy, since we modify errors
        traceback_cmp = _compare_traceback_strings(
            error.traceback, previous_error.traceback
        )
        if traceback_cmp is None:
          # We have multiple bad call sites, e.g.,
          #   def f(x):  x + 42
          #   f("hello")  # error
          #   f("world")  # same error, different backtrace
          # so we'll report this error multiple times with different backtraces.
          continue
        elif traceback_cmp < 0:
          # If the current traceback is shorter, use the current error instead
          # of the previous one.
          errors.remove(previous_error)
        else:
          # One of the previous errors has a shorter traceback than the current
          # one, so the latter can be discarded.
          break
      else:
        if len(errors) < MAX_TRACEBACKS:
          errors.append(error)
    return sum(unique_errors.values(), [])

  def _sorted_errors(self):
    return sorted(self._errors, key=lambda x: (x.filename or "", x.line))

  def print_to_stderr(self, *, color=True):
    self.print_to_file(sys.stderr, color=color)

  def __str__(self):
    f = io.StringIO()
    self.print_to_file(f)
    return f.getvalue()


class VmErrorLog(ErrorLog):
  """ErrorLog with methods for adding specific pytype errors."""

  def __init__(self, pp: pretty_printer_base.PrettyPrinterBase, src: str):
    super().__init__(src)
    self._pp = pp

  @property
  def pretty_printer(self) -> pretty_printer_base.PrettyPrinterBase:
    return self._pp

  @_error_name("pyi-error")
  def pyi_error(self, stack, name, error):
    self.error(
        stack, f"Couldn't import pyi for {name!r}", str(error), keyword=name
    )

  @_error_name("attribute-error")
  def _attribute_error(self, stack, binding, obj_repr, attr_name):
    """Log an attribute error."""
    if len(binding.variable.bindings) > 1:
      # Joining the printed types rather than merging them before printing
      # ensures that we print all of the options when 'Any' is among them.
      details = "In %s" % self._pp.join_printed_types(
          self._pp.print_type(v) for v in binding.variable.data
      )
    else:
      details = None
    self.error(
        stack,
        f"No attribute {attr_name!r} on {obj_repr}",
        details=details,
        keyword=attr_name,
    )

  @_error_name("not-writable")
  def not_writable(self, stack, obj, attr_name):
    obj_repr = self._pp.print_type(obj)
    self.error(
        stack,
        f"Can't assign attribute {attr_name!r} on {obj_repr}",
        keyword=attr_name,
        keyword_context=obj_repr,
    )

  @_error_name("module-attr")
  def _module_attr(self, stack, module_name, attr_name):
    self.error(
        stack,
        f"No attribute {attr_name!r} on module {module_name!r}",
        keyword=attr_name,
        keyword_context=module_name,
    )

  def attribute_error(self, stack, binding, attr_name):
    ep = error_printer.AttributeErrorPrinter(self._pp)
    recv = ep.print_receiver(binding.data, attr_name)
    if recv.obj_type == error_printer.BadAttrType.SYMBOL:
      details = f"No attribute {attr_name!r} on {recv.obj}"
      self._unsupported_operands(stack, attr_name, recv.obj, details=details)
    elif recv.obj_type == error_printer.BadAttrType.MODULE:
      self._module_attr(stack, recv.obj, attr_name)
    elif recv.obj_type == error_printer.BadAttrType.OBJECT:
      self._attribute_error(stack, binding, recv.obj, attr_name)
    else:
      assert False, recv.obj_type

  @_error_name("unbound-type-param")
  def unbound_type_param(self, stack, obj, attr_name, type_param_name):
    self.error(
        stack,
        f"Can't access attribute {attr_name!r} on {obj.name}",
        f"No binding for type parameter {type_param_name}",
        keyword=attr_name,
        keyword_context=obj.name,
    )

  @_error_name("name-error")
  def name_error(self, stack, name, details=None):
    self.error(
        stack, f"Name {name!r} is not defined", keyword=name, details=details
    )

  @_error_name("import-error")
  def import_error(self, stack, module_name):
    self.error(
        stack, f"Can't find module {module_name!r}.", keyword=module_name
    )

  def _invalid_parameters(self, stack, message, bad_call):
    """Log an invalid parameters error."""
    ret = error_printer.BadCallPrinter(self._pp, bad_call).print_call_details()
    details = "".join(
        [
            "       Expected: (",
            ret.expected,
            ")\n",
            "Actually passed: (",
            ret.actual,
            ")",
        ]
        + ret.error_details
    )
    self.error(stack, message, details, bad_call=bad_call)

  @_error_name("wrong-arg-count")
  def wrong_arg_count(self, stack, name, bad_call):
    message = "%s expects %d arg(s), got %d" % (
        _function_name(name, capitalize=True),
        bad_call.sig.mandatory_param_count(),
        len(bad_call.passed_args),
    )
    self._invalid_parameters(stack, message, bad_call)

  def _get_binary_operation(self, function_name, bad_call):
    """Return (op, left, right) if the function should be treated as a binop."""
    maybe_left_operand, _, f = function_name.rpartition(".")
    # Check that
    # (1) the function is bound to an object (the left operand),
    # (2) the function has a pretty representation,
    # (3) either there are exactly two passed args or the function is one we've
    #     chosen to treat as a binary operation.
    if (
        not maybe_left_operand
        or f not in slots.SYMBOL_MAPPING
        or (
            len(bad_call.passed_args) != 2
            and f not in ("__setitem__", "__getslice__")
        )
    ):
      return None
    for arg_name, arg_value in bad_call.passed_args[1:]:
      if arg_name == bad_call.bad_param.name:
        # maybe_left_operand is something like `dict`, but we want a more
        # precise type like `Dict[str, int]`.
        left_operand = self._pp.print_type(bad_call.passed_args[0][1])
        right_operand = self._pp.print_type(arg_value)
        return f, left_operand, right_operand
    return None

  def wrong_arg_types(self, stack, name, bad_call):
    """Log [wrong-arg-types]."""
    operation = self._get_binary_operation(name, bad_call)
    if operation:
      operator, left_operand, right_operand = operation
      operator_name = _function_name(operator, capitalize=True)
      expected_right_operand = self._pp.print_type_of_instance(
          bad_call.bad_param.typ
      )
      details = (
          f"{operator_name} on {left_operand} expects {expected_right_operand}"
      )
      self._unsupported_operands(
          stack, operator, left_operand, right_operand, details=details
      )
    else:
      self._wrong_arg_types(stack, name, bad_call)

  @_error_name("wrong-arg-types")
  def _wrong_arg_types(self, stack, name, bad_call):
    """A function was called with the wrong parameter types."""
    message = "%s was called with the wrong arguments" % _function_name(
        name, capitalize=True
    )
    self._invalid_parameters(stack, message, bad_call)

  @_error_name("wrong-keyword-args")
  def wrong_keyword_args(self, stack, name, bad_call, extra_keywords):
    """A function was called with extra keywords."""
    if len(extra_keywords) == 1:
      message = "Invalid keyword argument {} to {}".format(
          extra_keywords[0], _function_name(name)
      )
    else:
      message = "Invalid keyword arguments {} to {}".format(
          "(" + ", ".join(sorted(extra_keywords)) + ")", _function_name(name)
      )
    self._invalid_parameters(stack, message, bad_call)

  @_error_name("missing-parameter")
  def missing_parameter(self, stack, name, bad_call, missing_parameter):
    """A function call is missing parameters."""
    message = "Missing parameter {!r} in call to {}".format(
        missing_parameter, _function_name(name)
    )
    self._invalid_parameters(stack, message, bad_call)

  @_error_name("not-callable")
  def not_callable(self, stack, func, details=None):
    """Calling an object that isn't callable."""
    if isinstance(func, types.Function) and func.is_overload:
      prefix = "@typing.overload-decorated "
    else:
      prefix = ""
    message = f"{prefix}{func.name!r} object is not callable"
    self.error(stack, message, keyword=func.name, details=details)

  @_error_name("not-indexable")
  def not_indexable(self, stack, name, generic_warning=False):
    message = f"class {name} is not indexable"
    if generic_warning:
      self.error(
          stack, message, f"({name!r} does not subclass Generic)", keyword=name
      )
    else:
      self.error(stack, message, keyword=name)

  @_error_name("not-instantiable")
  def not_instantiable(self, stack, cls):
    """Instantiating an abstract class."""
    message = "Can't instantiate {} with abstract methods {}".format(
        cls.full_name, ", ".join(sorted(cls.abstract_methods))
    )
    self.error(stack, message)

  @_error_name("ignored-abstractmethod")
  def ignored_abstractmethod(self, stack, cls_name, method_name):
    message = f"Stray abc.abstractmethod decorator on method {method_name}"
    self.error(
        stack,
        message,
        details=f"({cls_name} does not have metaclass abc.ABCMeta)",
    )

  @_error_name("ignored-metaclass")
  def ignored_metaclass(self, stack, cls, metaclass):
    message = f"Metaclass {metaclass} on class {cls} ignored in Python 3"
    self.error(stack, message)

  @_error_name("duplicate-keyword-argument")
  def duplicate_keyword(self, stack, name, bad_call, duplicate):
    message = "%s got multiple values for keyword argument %r" % (
        _function_name(name),
        duplicate,
    )
    self._invalid_parameters(stack, message, bad_call)

  @_error_name("invalid-super-call")
  def invalid_super_call(self, stack, message, details=None):
    self.error(stack, message, details)

  def invalid_function_call(self, stack, error):
    """Log an invalid function call."""
    # Make sure method names are prefixed with the class name.
    if (
        isinstance(error, error_types.InvalidParameters)
        and "." not in error.name
        and error.bad_call.sig.param_names
        and error.bad_call.sig.param_names[0] in ("self", "cls")
        and error.bad_call.passed_args
    ):
      error.name = f"{error.bad_call.passed_args[0][1].full_name}.{error.name}"
    if isinstance(error, error_types.WrongArgCount):
      self.wrong_arg_count(stack, error.name, error.bad_call)
    elif isinstance(error, error_types.WrongArgTypes):
      self.wrong_arg_types(stack, error.name, error.bad_call)
    elif isinstance(error, error_types.WrongKeywordArgs):
      self.wrong_keyword_args(
          stack, error.name, error.bad_call, error.extra_keywords
      )
    elif isinstance(error, error_types.MissingParameter):
      self.missing_parameter(
          stack, error.name, error.bad_call, error.missing_parameter
      )
    elif isinstance(error, error_types.NotCallable):
      self.not_callable(stack, error.obj)
    elif isinstance(error, error_types.DuplicateKeyword):
      self.duplicate_keyword(stack, error.name, error.bad_call, error.duplicate)
    elif isinstance(error, error_types.UndefinedParameterError):
      self.name_error(stack, error.name)
    elif isinstance(error, error_types.TypedDictKeyMissing):
      self.typed_dict_error(stack, error.typed_dict, error.name)
    elif isinstance(error, error_types.DictKeyMissing):
      # We don't report DictKeyMissing because the false positive rate is high.
      pass
    else:
      raise AssertionError(error)

  @_error_name("base-class-error")
  def base_class_error(self, stack, base_var, details=None):
    base_cls = self._pp.join_printed_types(
        self._pp.print_type_of_instance(t) for t in base_var.data
    )
    self.error(
        stack,
        f"Invalid base class: {base_cls}",
        details=details,
        keyword=base_cls,
    )

  @_error_name("bad-return-type")
  def bad_return_type(self, stack, node, bad):
    """Logs a [bad-return-type] error."""

    ret = error_printer.MatcherErrorPrinter(self._pp).print_return_types(
        node, bad
    )
    if ret.full_actual == ret.bad_actual:
      message = "bad return type"
    else:
      message = f"bad option {ret.bad_actual!r} in return type"
    details = [
        "         Expected: ",
        ret.expected,
        "\n",
        "Actually returned: ",
        ret.full_actual,
    ]
    details.extend(ret.error_details)
    self.error(stack, message, "".join(details))

  @_error_name("bad-return-type")
  def any_return_type(self, stack):
    """Logs a [bad-return-type] error."""
    message = "Return type may not be Any"
    details = [
        "Pytype is running with features=no-return-any, which does "
        + "not allow Any as a return type."
    ]
    self.error(stack, message, "".join(details))

  @_error_name("bad-yield-annotation")
  def bad_yield_annotation(self, stack, name, annot, is_async):
    func = ("async " if is_async else "") + f"generator function {name}"
    actual = self._pp.print_type_of_instance(annot)
    message = f"Bad return type {actual!r} for {func}"
    if is_async:
      details = "Expected AsyncGenerator, AsyncIterable or AsyncIterator"
    else:
      details = "Expected Generator, Iterable or Iterator"
    self.error(stack, message, details)

  @_error_name("bad-concrete-type")
  def bad_concrete_type(self, stack, node, bad, details=None):
    ret = error_printer.MatcherErrorPrinter(self._pp).print_return_types(
        node, bad
    )
    full_details = [
        "       Expected: ",
        ret.expected,
        "\n",
        "Actually passed: ",
        ret.bad_actual,
    ]
    if details:
      full_details.append("\n" + details)
    full_details.extend(ret.error_details)
    self.error(
        stack, "Invalid instantiation of generic class", "".join(full_details)
    )

  def unsupported_operands(self, stack, operator, var1, var2):
    left = self._pp.show_variable(var1)
    right = self._pp.show_variable(var2)
    details = f"No attribute {operator!r} on {left}"
    if operator in slots.REVERSE_NAME_MAPPING:
      details += f" or {slots.REVERSE_NAME_MAPPING[operator]!r} on {right}"
    self._unsupported_operands(stack, operator, left, right, details=details)

  @_error_name("unsupported-operands")
  def _unsupported_operands(self, stack, operator, *operands, details=None):
    """Unsupported operands."""
    # `operator` is sometimes the symbol and sometimes the method name, so we
    # need to check for both here.
    # TODO(mdemello): This is a mess, we should fix the call sites.
    if operator in slots.SYMBOL_MAPPING:
      symbol = slots.SYMBOL_MAPPING[operator]
    else:
      symbol = operator
    cmp = operator in slots.COMPARES or symbol in slots.COMPARES
    args = " and ".join(str(operand) for operand in operands)
    if cmp:
      details = f"Types {args} are not comparable."
      self.error(
          stack, f"unsupported operand types for {symbol}", details=details
      )
    else:
      self.error(
          stack,
          f"unsupported operand type(s) for {symbol}: {args}",
          details=details,
      )

  def invalid_annotation(
      self,
      stack,
      annot: str | types.BaseValue | None,
      details=None,
      name=None,
  ):
    if isinstance(annot, types.BaseValue):
      annot = self._pp.print_type_of_instance(annot)
    self._invalid_annotation(stack, annot, details, name)

  def _print_params_helper(self, param_or_params):
    if isinstance(param_or_params, types.BaseValue):
      return self._pp.print_type_of_instance(param_or_params)
    else:
      return "[{}]".format(
          ", ".join(self._print_params_helper(p) for p in param_or_params)
      )

  def wrong_annotation_parameter_count(
      self,
      stack,
      annot: types.BaseValue,
      params: Sequence[types.BaseValue],
      expected_count: int,
      template: Iterable[str] | None = None,
  ):
    """Log an error for an annotation with the wrong number of parameters."""
    base_type = self._pp.print_type_of_instance(annot)
    full_type = base_type + self._print_params_helper(params)
    if template:
      templated_type = f"{base_type}[{', '.join(template)}]"
    else:
      templated_type = base_type
    details = "%s expected %d parameter%s, got %d" % (
        templated_type,
        expected_count,
        "" if expected_count == 1 else "s",
        len(params),
    )
    self._invalid_annotation(stack, full_type, details, name=None)

  def invalid_ellipses(self, stack, indices, container_name):
    if indices:
      details = "Not allowed at {} {} in {}".format(
          "index" if len(indices) == 1 else "indices",
          ", ".join(str(i) for i in sorted(indices)),
          container_name,
      )
      self._invalid_annotation(stack, "Ellipsis", details, None)

  def ambiguous_annotation(
      self,
      stack,
      options: str | Iterable[types.BaseValue] | None,
      name=None,
  ):
    """Log an ambiguous annotation."""
    if isinstance(options, (str, type(None))):
      desc = options
    else:
      desc = " or ".join(
          sorted(self._pp.print_type_of_instance(o) for o in options)
      )
    self._invalid_annotation(stack, desc, "Must be constant", name)

  @_error_name("invalid-annotation")
  def _invalid_annotation(self, stack, annot_string, details, name):
    """Log the invalid annotation."""
    if name is None:
      suffix = ""
    else:
      suffix = "for " + name
    annot_string = f"{annot_string!r} " if annot_string else ""
    self.error(
        stack,
        f"Invalid type annotation {annot_string}{suffix}",
        details=details,
    )

  @_error_name("mro-error")
  def mro_error(self, stack, name, mro_seqs, details=None):
    seqs = []
    for seq in mro_seqs:
      seqs.append(f"[{', '.join(cls.name for cls in seq)}]")
    suffix = f": {', '.join(seqs)}" if seqs else ""
    msg = f"{name} has invalid inheritance{suffix}."
    self.error(stack, msg, keyword=name, details=details)

  @_error_name("invalid-directive")
  def invalid_directive(self, filename, line, message):
    self._add(
        Error(
            SEVERITY_WARNING,
            message,
            src=self._src,
            filename=filename,
            line=line,
        )
    )

  @_error_name("late-directive")
  def late_directive(self, filename, line, name):
    message = f"{name} disabled from here to the end of the file"
    details = (
        "Consider limiting this directive's scope or moving it to the "
        "top of the file."
    )
    self._add(
        Error(
            SEVERITY_WARNING,
            message,
            src=self._src,
            details=details,
            filename=filename,
            line=line,
        )
    )

  @_error_name("not-supported-yet")
  def not_supported_yet(self, stack, feature, details=None):
    self.error(stack, f"{feature} not supported yet", details=details)

  @_error_name("python-compiler-error")
  def python_compiler_error(self, filename, line, message):
    self._add(
        Error(
            SEVERITY_ERROR, message, filename=filename, line=line, src=self._src
        )
    )

  @_error_name("recursion-error")
  def recursion_error(self, stack, name):
    self.error(stack, f"Detected recursion in {name}", keyword=name)

  @_error_name("redundant-function-type-comment")
  def redundant_function_type_comment(self, filename, line):
    self._add(
        Error(
            SEVERITY_ERROR,
            "Function type comments cannot be used with annotations",
            filename=filename,
            line=line,
            src=self._src,
        )
    )

  @_error_name("invalid-function-type-comment")
  def invalid_function_type_comment(self, stack, comment, details=None):
    self.error(
        stack, f"Invalid function type comment: {comment}", details=details
    )

  @_error_name("ignored-type-comment")
  def ignored_type_comment(self, filename, line, comment):
    self._add(
        Error(
            SEVERITY_WARNING,
            f"Stray type comment: {comment}",
            filename=filename,
            line=line,
            src=self._src,
        )
    )

  @_error_name("invalid-typevar")
  def invalid_typevar(self, stack, comment, bad_call=None):
    if bad_call:
      self._invalid_parameters(stack, comment, bad_call)
    else:
      self.error(stack, f"Invalid TypeVar: {comment}")

  @_error_name("invalid-namedtuple-arg")
  def invalid_namedtuple_arg(self, stack, badname=None, err_msg=None):
    if err_msg is None:
      msg = (
          "collections.namedtuple argument %r is not a valid typename or "
          "field name."
      )
      self.warn(stack, msg % badname)
    else:
      self.error(stack, err_msg)

  @_error_name("bad-function-defaults")
  def bad_function_defaults(self, stack, func_name):
    msg = "Attempt to set %s.__defaults__ to a non-tuple value."
    self.warn(stack, msg % func_name)

  @_error_name("bad-slots")
  def bad_slots(self, stack, msg):
    self.error(stack, msg)

  @_error_name("bad-unpacking")
  def bad_unpacking(self, stack, num_vals, num_vars):
    prettify = lambda v, label: "%d %s%s" % (v, label, "" if v == 1 else "s")
    vals_str = prettify(num_vals, "value")
    vars_str = prettify(num_vars, "variable")
    msg = f"Cannot unpack {vals_str} into {vars_str}"
    self.error(stack, msg, keyword=vals_str)

  @_error_name("bad-unpacking")
  def nondeterministic_unpacking(self, stack):
    self.error(stack, "Unpacking a non-deterministic order iterable.")

  @_error_name("reveal-type")
  def reveal_type(self, stack, node, var):
    self.error(stack, self._pp.print_var_type(var, node))

  @_error_name("assert-type")
  def assert_type(self, stack, actual: str, expected: str):
    """Check that a variable type matches its expected value."""
    details = f"Expected: {expected}\n  Actual: {actual}"
    self.error(stack, actual, details=details)

  @_error_name("annotation-type-mismatch")
  def annotation_type_mismatch(
      self,
      stack,
      annot,
      binding,
      name,
      error_details,
      details=None,
      *,
      typed_dict=None,
  ):
    """Invalid combination of annotation and assignment."""
    if annot is None:
      return
    annot_string = self._pp.print_type_of_instance(annot)
    literal = "Literal[" in annot_string
    actual_string = self._pp.print_type(binding.data, literal=literal)
    if actual_string == "None":
      annot_string += f" (Did you mean '{annot_string} | None'?)"
    additional_details = f"\n\n{details}" if details else ""
    pp = error_printer.MatcherErrorPrinter(self._pp)
    additional_details += "".join(pp.print_error_details(error_details))
    details = (
        f"Annotation: {annot_string}\n"
        + f"Assignment: {actual_string}"
        + additional_details
    )
    if len(binding.variable.bindings) > 1:
      # Joining the printed types rather than merging them before printing
      # ensures that we print all of the options when 'Any' is among them.
      # We don't need to print this if there is only 1 unique type.
      print_types = {
          self._pp.print_type(v, literal=literal) for v in binding.variable.data
      }
      if len(print_types) > 1:
        details += (
            "\nIn assignment of type: "
            f"{self._pp.join_printed_types(print_types)}"
        )
    if typed_dict is not None:
      suffix = f" for key {name} in TypedDict {typed_dict.class_name}"
    elif name is not None:
      suffix = " for " + name
    else:
      suffix = ""
    err_msg = f"Type annotation{suffix} does not match type of assignment"
    self.error(stack, err_msg, details=details)

  @_error_name("container-type-mismatch")
  def container_type_mismatch(self, stack, cls, mutations, name):
    """Invalid combination of annotation and mutation.

    Args:
      stack: the frame stack
      cls: the container type
      mutations: a dict of {parameter name: (annotated types, new types)}
      name: the variable name (or None)
    """
    details = f"Container: {self._pp.print_generic_type(cls)}\n"
    allowed_contained = ""
    new_contained = ""
    for formal in cls.formal_type_parameters.keys():
      if formal in mutations:
        params, values, _ = mutations[formal]
        allowed_content = self._pp.print_type_of_instance(
            cls.get_formal_type_parameter(formal)
        )
        new_content = self._pp.join_printed_types(
            sorted(
                self._pp.print_type(v)
                for v in set(values.data) - set(params.data)
            )
        )
        allowed_contained += f"  {formal}: {allowed_content}\n"
        new_contained += f"  {formal}: {new_content}\n"
    annotation = self._pp.print_type_of_instance(cls)
    details += (
        "Allowed contained types (from annotation %s):\n%s"
        "New contained types:\n%s"
    ) % (annotation, allowed_contained, new_contained)
    suffix = "" if name is None else " for " + name
    err_msg = f"New container type{suffix} does not match type annotation"
    self.error(stack, err_msg, details=details)

  @_error_name("invalid-function-definition")
  def invalid_function_definition(self, stack, msg, details=None):
    self.error(stack, msg, details=details)

  @_error_name("invalid-signature-mutation")
  def invalid_signature_mutation(self, stack, func_name, sig):
    sig = self._pp.print_pytd(sig)
    msg = "Invalid self type mutation in pyi method signature"
    details = f"{func_name}{sig}"
    self.error(stack, msg, details)

  @_error_name("typed-dict-error")
  def typed_dict_error(self, stack, obj, name):
    """Accessing a nonexistent key in a typed dict.

    Args:
      stack: the frame stack
      obj: the typed dict instance
      name: the key name
    """
    if name:
      err_msg = f"TypedDict {obj.class_name} does not contain key {name}"
    else:
      err_msg = (
          f"TypedDict {obj.class_name} requires all keys to be constant strings"
      )
    self.error(stack, err_msg)

  @_error_name("final-error")
  def _overriding_final(self, stack, cls, base, name, *, is_method, details):
    desc = "method" if is_method else "class attribute"
    msg = (
        f"Class {cls.name} overrides final {desc} {name}, "
        f"defined in base class {base.name}"
    )
    self.error(stack, msg, details=details)

  def overriding_final_method(self, stack, cls, base, name, details=None):
    self._overriding_final(
        stack, cls, base, name, details=details, is_method=True
    )

  def overriding_final_attribute(self, stack, cls, base, name, details=None):
    self._overriding_final(
        stack, cls, base, name, details=details, is_method=False
    )

  def _normalize_signature(self, signature):
    """If applicable, converts from `f(self: A, ...)` to `A.f(self, ...)`."""
    self_name = signature.param_names and signature.param_names[0]
    if "." not in signature.name and self_name in signature.annotations:
      annotations = dict(signature.annotations)
      self_annot = annotations.pop(self_name)
      signature = signature._replace(
          name=f"{self_annot.full_name}.{signature.name}",
          annotations=annotations,
      )
    return signature

  @_error_name("signature-mismatch")
  def overriding_signature_mismatch(
      self, stack, base_signature, class_signature, details=None
  ):
    """Signature mismatch between overridden and overriding class methods."""
    base_signature = self._normalize_signature(base_signature)
    class_signature = self._normalize_signature(class_signature)
    signatures = (
        f"Base signature: '{base_signature}'.\n"
        f"Subclass signature: '{class_signature}'."
    )
    if details:
      details = signatures + "\n" + details
    else:
      details = signatures
    self.error(stack, "Overriding method signature mismatch", details=details)

  @_error_name("final-error")
  def assigning_to_final(self, stack, name, local):
    """Attempting to reassign a variable annotated with Final."""
    obj = "variable" if local else "attribute"
    err_msg = f"Assigning to {obj} {name}, which was annotated with Final"
    self.error(stack, err_msg)

  @_error_name("final-error")
  def subclassing_final_class(self, stack, base_var, details=None):
    base_cls = self._pp.join_printed_types(
        self._pp.print_type_of_instance(t) for t in base_var.data
    )
    self.error(
        stack,
        f"Cannot subclass final class: {base_cls}",
        details=details,
        keyword=base_cls,
    )

  @_error_name("final-error")
  def bad_final_decorator(self, stack, obj, details=None):
    name = getattr(obj, "name", None)
    if not name:
      typ = self._pp.print_type_of_instance(obj)
      name = f"object of type {typ}"
    msg = f"Cannot apply @final decorator to {name}"
    details = "@final can only be applied to classes and methods."
    self.error(stack, msg, details=details)

  @_error_name("final-error")
  def invalid_final_type(self, stack, details=None):
    msg = "Invalid use of typing.Final"
    details = (
        "Final may only be used as the outermost type in assignments "
        "or variable annotations."
    )
    self.error(stack, msg, details=details)

  @_error_name("match-error")
  def match_posargs_count(self, stack, cls, posargs, match_args, details=None):
    msg = (
        f"{cls.name}() accepts {match_args} positional sub-patterns"
        f" ({posargs} given)"
    )
    self.error(stack, msg, details=details)

  @_error_name("match-error")
  def bad_class_match(self, stack, obj, details=None):
    msg = f"Invalid constructor pattern in match case (not a class): {obj}"
    self.error(stack, msg, details=details)

  @_error_name("incomplete-match")
  def incomplete_match(self, stack, line, cases, details=None):
    cases = ", ".join(str(x) for x in cases)
    msg = f"The match is missing the following cases: {cases}"
    self.error(stack, msg, details=details, line=line)

  @_error_name("redundant-match")
  def redundant_match(self, stack, case, details=None):
    msg = f"This case has already been covered: {case}."
    self.error(stack, msg, details=details)

  @_error_name("paramspec-error")
  def paramspec_error(self, stack, details=None):
    msg = "ParamSpec error"
    self.error(stack, msg, details=details)

  @_error_name("dataclass-error")
  def dataclass_error(self, stack, details=None):
    msg = "Dataclass error"
    self.error(stack, msg, details=details)

  @_error_name("override-error")
  def no_overridden_attribute(self, stack, attr):
    msg = f"Attribute {attr!r} not found on any parent class"
    self.error(stack, msg)

  @_error_name("override-error")
  def missing_override_decorator(self, stack, attr, parent):
    parent_attr = f"{parent}.{attr}"
    msg = (
        f"Missing @typing.override decorator for {attr!r}, which overrides "
        f"{parent_attr!r}"
    )
    self.error(stack, msg)


def get_error_names_set():
  return _ERROR_NAMES
