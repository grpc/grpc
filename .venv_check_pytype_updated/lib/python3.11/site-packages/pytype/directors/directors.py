"""Code and data structures for managing source directives."""

import bisect
import collections
import logging
import sys


from pytype import config
from pytype.directors import annotations

from pytype.directors import parser
# pylint: enable=g-import-not-at-top

log = logging.getLogger(__name__)

SkipFileError = parser.SkipFileError
parse_src = parser.parse_src

_ALL_ERRORS = "*"  # Wildcard for disabling all errors.

_ALLOWED_FEATURES = frozenset(x.flag for x in config.FEATURE_FLAGS)

_PRAGMAS = frozenset({"cache-return"})

_FUNCTION_CALL_ERRORS = frozenset((
    # A function call may implicitly access a magic method attribute.
    "attribute-error",
    "duplicate-keyword",
    # Subscripting an annotation is a __getitem__ call.
    "invalid-annotation",
    "missing-parameter",
    "not-instantiable",
    "wrong-arg-count",
    "wrong-arg-types",
    "wrong-keyword-args",
    "unsupported-operands",
))

_ALL_ADJUSTABLE_ERRORS = _FUNCTION_CALL_ERRORS.union((
    "annotation-type-mismatch",
    "bad-return-type",
    "bad-yield-annotation",
    "container-type-mismatch",
    "not-supported-yet",
    "signature-mismatch",
))


class _DirectiveError(Exception):
  pass


class _LineSet:
  """A set of line numbers.

  The data structure is optimized to represent the union of a sparse set
  of integers and ranges of non-negative integers.  This supports the two styles
  of directives: those after a statement apply only to that line and those on
  their own line apply until countered by the opposing directive.
  """

  def __init__(self):
    # Map of line->bool for specific lines, takes precedence over _transitions.
    self._lines = {}
    # A sorted list of the lines at which the range state changes
    # polarity.  It is assumed to initially be false (not in a range).
    # Even positions represent the start of a range, odd positions represent
    # the end of a range.  Thus [2, 5, 10, 12] would include lines 2, 3, 4, 10,
    # and 11.  If the length is odd, then an end of maxint is implied, thus
    # [2, 5, 10] would disable lines 2, 3, 4, 10, 11, 12, ...
    self._transitions = []

  @property
  def lines(self):
    return self._lines

  def set_line(self, line, membership):
    """Set whether a given line is a member of the set."""
    self._lines[line] = membership

  def start_range(self, line, membership):
    """Start a range of lines that are either included/excluded from the set.

    Args:
      line: A line number.
      membership: If True, lines >= line are included in the set (starting a
        range), otherwise they are excluded (ending a range).

    Raises:
      ValueError: if line is less than that of a previous call to start_range().
    """
    last = self._transitions[-1] if self._transitions else -1
    # Assert that lines are monotonically increasing.  This simplifies the
    # logic of adding new lines and ensures that _ranges is sorted.
    if line < last:
      raise ValueError("Line number less than previous start_range() call.")
    # Determine previous membership state (True if the last range has an
    # indefinite end).
    previous = (len(self._transitions) % 2) == 1
    if membership == previous:
      return  # Redundant with previous state, do nothing.
    elif line == last:
      # We have either enable/disable or disable/enable on the same line,
      # cancel them out by popping the previous transition.
      self._transitions.pop()
    else:
      # Normal case - add a transition at this line.
      self._transitions.append(line)

  def __contains__(self, line):
    """Return if a line is a member of the set."""
    # First check for an entry in _lines.
    specific = self._lines.get(line)
    if specific is not None:
      return specific
    # Find the position in _ranges for line.  The polarity of this position
    # determines whether we are inside a range (odd) or outside (even).
    pos = bisect.bisect(self._transitions, line)
    return (pos % 2) == 1

  def get_disable_after(self, line):
    """Get an unclosed disable, if any, that starts after line."""
    if len(self._transitions) % 2 == 1 and self._transitions[-1] >= line:
      return self._transitions[-1]
    return None


class _BlockRanges:
  """A collection of possibly nested start..end ranges from AST nodes."""

  def __init__(self, start_to_end_mapping):
    self._starts = sorted(start_to_end_mapping)
    self._start_to_end = start_to_end_mapping
    self._end_to_start = {v: k for k, v in start_to_end_mapping.items()}

  def has_start(self, line):
    return line in self._start_to_end

  def has_end(self, line):
    return line in self._end_to_start

  def find_outermost(self, line):
    """Find the outermost interval containing line."""
    i = bisect.bisect_left(self._starts, line)
    num_intervals = len(self._starts)
    if i or line == self._starts[0]:
      if i < num_intervals and self._starts[i] == line:
        # line number is start of interval.
        start = self._starts[i]
      else:
        # Skip nested intervals
        while (
            1 < i <= num_intervals
            and self._start_to_end[self._starts[i - 1]] < line
        ):
          i -= 1
        start = self._starts[i - 1]
      end = self._start_to_end[start]
      if line in range(start, end):
        return start, end
    return None, None

  def adjust_end(self, old_end, new_end):
    start = self._end_to_start[old_end]
    self._start_to_end[start] = new_end
    del self._end_to_start[old_end]
    self._end_to_start[new_end] = start


class Director:
  """Holds all of the directive information for a source file."""

  def __init__(self, src_tree, errorlog, filename, disable):
    """Create a Director for a source file.

    Args:
      src_tree: The source text as an ast.
      errorlog: An ErrorLog object.  Directive errors will be logged to the
        errorlog.
      filename: The name of the source file.
      disable: List of error messages to always ignore.
    """
    self._filename = filename
    self._errorlog = errorlog
    # Collects type comments and variable annotations by line number
    self._variable_annotations = annotations.VariableAnnotations()
    self._param_annotations = None
    # Lines that have "type: ignore".  These will disable all errors, and in
    # the future may have other impact (such as not attempting an import).
    self._ignore = _LineSet()
    # Map from error name to lines for which that error is disabled.  Note
    # that _ALL_ERRORS is essentially a wildcard name (it matches all names).
    self._disables = collections.defaultdict(_LineSet)
    # Map from pragma to lines for which that pragma is set
    self._pragmas = collections.defaultdict(_LineSet)
    # Function line number -> decorators map.
    self._decorators = collections.defaultdict(list)
    # Decorator line number -> decorated function line number map.
    self._decorated_functions = {}
    # Apply global disable, from the command line arguments:
    for error_name in disable:
      self._disables[error_name].start_range(0, True)
    # Store function ranges and return lines to distinguish explicit and
    # implicit returns (the bytecode has a `RETURN None` for implcit returns).
    self.return_lines = set()
    self.block_returns = None
    self._function_ranges = _BlockRanges({})
    # Parse the source code for directives.
    self._parse_src_tree(src_tree)

  @property
  def type_comments(self):
    return self._variable_annotations.type_comments

  @property
  def annotations(self):
    return self._variable_annotations.annotations

  @property
  def param_annotations(self):
    ret = {}
    for a in self._param_annotations:
      for i in range(a.start_line, a.end_line):
        ret[i] = a.annotations
    return ret

  @property
  def ignore(self):
    return self._ignore

  @property
  def decorators(self):
    return self._decorators

  @property
  def decorated_functions(self):
    return self._decorated_functions

  def has_pragma(self, pragma, line):
    return pragma in self._pragmas and line in self._pragmas[pragma]

  def _parse_src_tree(self, src_tree):
    """Parse a source file, extracting directives from comments."""
    visitor = parser.visit_src_tree(src_tree)
    # TODO(rechen): This check can be removed once parser_libcst is gone.
    if not visitor:
      return

    self.block_returns = visitor.block_returns
    self.return_lines = visitor.block_returns.all_returns()
    self._function_ranges = _BlockRanges(visitor.function_ranges)
    self._param_annotations = visitor.param_annotations
    self.matches = visitor.matches
    self.features = set()

    for line_range, group in visitor.structured_comment_groups.items():
      for comment in group:
        if comment.tool == "type":
          self._process_type(
              comment.line, comment.data, comment.open_ended, line_range
          )
        else:
          assert comment.tool == "pytype"
          try:
            self._process_pytype(
                comment.line, comment.data, comment.open_ended, line_range
            )
          except _DirectiveError as e:
            self._errorlog.invalid_directive(
                self._filename, comment.line, str(e)
            )
        # Make sure the function range ends at the last "interesting" line.
        if not isinstance(
            line_range, parser.Call
        ) and self._function_ranges.has_end(line_range.end_line):
          end = line_range.start_line
          self._function_ranges.adjust_end(line_range.end_line, end)

    for annot in visitor.variable_annotations:
      self._variable_annotations.add_annotation(
          annot.start_line, annot.name, annot.annotation
      )

    for line, decorators in visitor.decorators.items():
      for decorator_lineno, decorator_name in decorators:
        self._decorators[line].append(decorator_name)
        self._decorated_functions[decorator_lineno] = line

    if visitor.defs_start is not None:
      disables = list(self._disables.items())
      # Add "# type: ignore" to the list of disables that we check.
      disables.append(("Type checking", self._ignore))
      for name, lineset in disables:
        line = lineset.get_disable_after(visitor.defs_start)
        if line is not None:
          self._errorlog.late_directive(self._filename, line, name)

  def _process_type(
      self, line: int, data: str, open_ended: bool, line_range: parser.LineRange
  ):
    """Process a type: comment."""
    is_ignore = parser.IGNORE_RE.match(data)
    if not is_ignore and line != line_range.end_line:
      # Warn and discard type comments placed in the middle of expressions.
      self._errorlog.ignored_type_comment(self._filename, line, data)
      return
    final_line = line_range.start_line
    if is_ignore:
      if open_ended:
        self._ignore.start_range(line, True)
      else:
        self._ignore.set_line(line, True)
        self._ignore.set_line(final_line, True)
    else:
      if final_line in self._variable_annotations.type_comments:
        # If we have multiple type comments on the same line, take the last one,
        # but add an error to the log.
        self._errorlog.invalid_directive(
            self._filename, line, "Multiple type comments on the same line."
        )
      self._variable_annotations.add_type_comment(final_line, data)

  def _process_pytype(
      self, line: int, data: str, open_ended: bool, line_range: parser.LineRange
  ):
    """Process a pytype: comment."""
    if not data:
      raise _DirectiveError("Invalid directive syntax.")
    for option in data.split():
      # Parse the command.
      try:
        command, values = option.split("=", 1)
        values = values.split(",")
      except ValueError as e:
        raise _DirectiveError("Invalid directive syntax.") from e
      # Additional commands may be added in the future.  For now, only
      # "disable", "enable", "pragma", and "features" are supported.
      values = set(values)
      if command == "disable":
        self._process_disable(
            line, line_range, open_ended, values, disable=True
        )
      elif command == "enable":
        self._process_disable(
            line, line_range, open_ended, values, disable=False
        )
      elif command == "pragma":
        self._process_pragmas(line, line_range, values)
      elif command == "features":
        self._process_features(values)
        continue
      else:
        raise _DirectiveError(f"Unknown pytype directive: '{command}'")

  def _process_features(self, features: set[str]):
    invalid = features - _ALLOWED_FEATURES
    if invalid:
      raise _DirectiveError(f"Unknown pytype features: {','.join(invalid)}")
    self.features |= features

  def _process_pragmas(
      self, line: int, line_range: parser.LineRange, pragmas: set[str]
  ):
    del line_range  # unused
    invalid = pragmas - _PRAGMAS
    if invalid:
      raise _DirectiveError(f"Unknown pytype pragmas: {','.join(invalid)}")
    for pragma in pragmas:
      lines = self._pragmas[pragma]
      lines.set_line(line, True)

  def _process_disable(
      self,
      line: int,
      line_range: parser.LineRange,
      open_ended: bool,
      values: set[str],
      *,
      disable: bool,
  ):
    """Process enable/disable directives."""

    def keep(error_name):
      if isinstance(line_range, parser.Call):
        return error_name in _FUNCTION_CALL_ERRORS
      else:
        return True

    if not values:
      raise _DirectiveError(
          "Disable/enable must specify one or more error names."
      )

    for error_name in values:
      if error_name == _ALL_ERRORS or self._errorlog.is_valid_error_name(
          error_name
      ):
        if not keep(error_name):
          # Skip the directive if we are in a line range that is irrelevant to
          # it. (Every directive is also recorded in a base LineRange that is
          # never skipped.)
          continue
        lines = self._disables[error_name]
        if open_ended:
          lines.start_range(line, disable)
        else:
          final_line = self._adjust_line_number_for_pytype_directive(
              line, error_name, line_range
          )
          if final_line != line:
            # Set the disable on the original line so that, even if we mess up
            # adjusting the line number, silencing an error by adding a
            # disable to the exact line the error is reported on always works.
            lines.set_line(line, disable)
          lines.set_line(final_line, disable)
      else:
        self._errorlog.invalid_directive(
            self._filename, line, f"Invalid error name: '{error_name}'"
        )

  def _adjust_line_number_for_pytype_directive(
      self, line: int, error_class: str, line_range: parser.LineRange
  ):
    """Adjusts the line number for a pytype directive."""
    if error_class not in _ALL_ADJUSTABLE_ERRORS:
      return line
    return line_range.start_line

  def filter_error(self, error):
    """Return whether the error should be logged.

    This method is suitable for use as an error filter.

    Args:
      error: An error._Error object.

    Returns:
      True iff the error should be included in the log.
    """
    # Always report errors that aren't for this file or do not have a line
    # number.
    if error.filename != self._filename or error.line is None:
      return True
    if (
        error.name == "bad-return-type"
        and error.opcode_name in ("RETURN_VALUE", "RETURN_CONST")
        and error.line not in self.return_lines
    ):
      # We have an implicit "return None". Adjust the line number to the last
      # line of the function.
      _, end = self._function_ranges.find_outermost(error.line)
      if end:
        error.set_line(end)
    # Treat line=0 as below the file, so we can filter it.
    line = error.line or sys.maxsize
    # Report the error if it isn't subject to any ignore or disable.
    return (
        line not in self._ignore
        and line not in self._disables[_ALL_ERRORS]
        and line not in self._disables[error.name]
    )
