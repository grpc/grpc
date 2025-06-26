"""Source code parser."""

import ast
import collections
from collections.abc import Mapping, Sequence
import dataclasses
import io
import logging
import re
import tokenize

from pytype.ast import visitor

log = logging.getLogger(__name__)

# Also supports mypy-style ignore[code, ...] syntax, treated as regular ignores.
IGNORE_RE = re.compile(r"^ignore(\[.+\])?$")

_DIRECTIVE_RE = re.compile(r"#\s*(pytype|type)\s*:\s?([^#]*)")


class SkipFileError(Exception):
  """Exception thrown if we encounter "pytype: skip-file" in the source code."""


@dataclasses.dataclass(frozen=True)
class LineRange:
  start_line: int
  end_line: int

  @classmethod
  def from_node(cls, node):
    return cls(node.lineno, node.end_lineno)

  def __contains__(self, line):
    return self.start_line <= line <= self.end_line


@dataclasses.dataclass(frozen=True)
class Call(LineRange):
  """Tag to identify function calls."""


@dataclasses.dataclass(frozen=True)
class _StructuredComment:
  """A structured comment.

  Attributes:
    line: The line number.
    tool: The tool label, e.g., "type" for "# type: int".
    data: The data, e.g., "int" for "# type: int".
    open_ended: True if the comment appears on a line by itself (i.e., it is
      open-ended rather than attached to a line of code).
  """

  line: int
  tool: str
  data: str
  open_ended: bool


@dataclasses.dataclass(frozen=True)
class _VariableAnnotation(LineRange):
  name: str
  annotation: str


@dataclasses.dataclass(frozen=True)
class _ParamAnnotations(LineRange):
  name: str
  annotations: dict[str, str]


@dataclasses.dataclass(frozen=True)
class _SourceTree:
  ast: ast.AST
  structured_comments: Mapping[int, Sequence[_StructuredComment]]


class _BlockReturns:
  """Tracks return statements in with/try blocks."""

  def __init__(self):
    self._block_ranges = []
    self._returns = []
    self._block_returns = {}
    self._final = False

  def add_block(self, node):
    line_range = LineRange.from_node(node)
    self._block_ranges.append(line_range)

  def add_return(self, node):
    self._returns.append(node.lineno)

  def finalize(self):
    for br in self._block_ranges:
      self._block_returns[br.start_line] = sorted(
          r for r in self._returns if r in br
      )
    self._final = True

  def all_returns(self):
    return set(self._returns)

  def __iter__(self):
    assert self._final
    return iter(self._block_returns.items())

  def __repr__(self):
    return f"""
      Blocks: {self._block_ranges}
      Returns: {self._returns}
      {self._block_returns}
    """


@dataclasses.dataclass
class _MatchCase:
  start: int
  end: int
  as_name: str | None
  is_underscore: bool
  match_line: int


@dataclasses.dataclass
class _Match:
  start: int
  end: int
  cases: list[_MatchCase]


class _Matches:
  """Tracks branches of match statements."""

  def __init__(self):
    self.matches = []

  def add_match(self, start, end, cases):
    self.matches.append(_Match(start, end, cases))


class _ParseVisitor(visitor.BaseVisitor):
  """Visitor for parsing a source tree.

  Attributes:
    structured_comment_groups: Ordered map from a line range to the "type:" and
      "pytype:" comments within the range. Line ranges come in several flavors:
      * Instances of the base LineRange class represent single logical
        statements. These ranges are ascending and non-overlapping and record
        all structured comments found.
      * Instances of the Call subclass represent function calls. These ranges
        are ascending by start_line but may overlap and only record "pytype:"
        comments.
    variable_annotations: Sequence of PEP 526-style variable annotations with
      line numbers.
    param_annotations: Similar to variable_annotations, but for function params.
    decorators: Sequence of lines at which decorated functions are defined.
    defs_start: The line number at which the first class or function definition
      appears, if any.
  """

  def __init__(self, raw_structured_comments):
    super().__init__(ast)
    self._raw_structured_comments = raw_structured_comments
    # We initialize structured_comment_groups with single-line groups for all
    # structured comments so that we don't accidentally lose any. These groups
    # will be merged into larger line ranges as the visitor runs.
    self.structured_comment_groups = collections.OrderedDict(
        (LineRange(lineno, lineno), list(structured_comments))
        for lineno, structured_comments in raw_structured_comments.items()
    )
    self.variable_annotations = []
    self.param_annotations = []
    self.decorators = collections.defaultdict(list)
    self.defs_start = None
    self.function_ranges = {}
    self.block_returns = _BlockReturns()
    self.block_depth = 0
    self.matches = _Matches()

  def _add_structured_comment_group(self, start_line, end_line, cls=LineRange):
    """Adds an empty _StructuredComment group with the given line range."""
    if cls is LineRange:
      # Check if the given line range is contained within an existing line
      # range. Since the visitor processes the source file roughly from top to
      # bottom, the existing range, if any, should be within a recently added
      # comment group. We also keep the groups ordered. So we do a reverse
      # search and stop as soon as we hit a statement that does not overlap with
      # the given range.
      for line_range, group in reversed(self.structured_comment_groups.items()):
        if isinstance(line_range, Call):
          continue
        if (
            line_range.start_line <= start_line
            and end_line <= line_range.end_line
        ):
          return group
        if line_range.end_line < start_line:
          break
    # We keep structured_comment_groups ordered by inserting the new line range
    # at the end, then absorbing line ranges that the new range contains and
    # calling move_to_end() on ones that should come after it. We encounter line
    # ranges in roughly ascending order, so this reordering is not expensive.
    keys_to_absorb = []
    keys_to_move = []
    for line_range in reversed(self.structured_comment_groups):
      if (
          cls is LineRange
          and start_line <= line_range.start_line
          and line_range.end_line <= end_line
      ):
        if type(line_range) is LineRange:  # pylint: disable=unidiomatic-typecheck
          keys_to_absorb.append(line_range)
        else:
          keys_to_move.append(line_range)
      elif line_range.start_line > start_line:
        keys_to_move.append(line_range)
      else:
        break
    self.structured_comment_groups[cls(start_line, end_line)] = new_group = []
    for k in reversed(keys_to_absorb):
      new_group.extend(self.structured_comment_groups[k])
      del self.structured_comment_groups[k]
    for k in reversed(keys_to_move):
      self.structured_comment_groups.move_to_end(k)
    return new_group

  def _process_structured_comments(self, line_range, cls=LineRange):

    def should_add(comment, group):
      # Don't add the comment more than once.
      if comment in group:
        return False
      # Open-ended comments are added in __init__.
      if comment.open_ended:
        return False
      # A "type: ignore" or "pytype:" comment can belong to any number of
      # overlapping function calls.
      return (
          comment.tool == "pytype"
          or comment.tool == "type"
          and IGNORE_RE.match(comment.data)
      )

    for lineno, structured_comments in self._raw_structured_comments.items():
      if lineno > line_range.end_line:
        # _raw_structured_comments is ordered by line number, so we can abort as
        # soon as we overshoot the node's line range.
        break
      if lineno < line_range.start_line:
        continue
      group = self._add_structured_comment_group(
          line_range.start_line, line_range.end_line, cls
      )
      # Comments do not need to be added to LineRange groups because we already
      # did so in __init__.
      if cls is not LineRange:
        group.extend(c for c in structured_comments if should_add(c, group))

  def leave_Module(self, node):
    self.block_returns.finalize()

  def visit_Call(self, node):
    self._process_structured_comments(LineRange.from_node(node), cls=Call)

  def visit_Compare(self, node):
    self._process_structured_comments(LineRange.from_node(node), cls=Call)

  def visit_Subscript(self, node):
    self._process_structured_comments(LineRange.from_node(node), cls=Call)

  def visit_AnnAssign(self, node):
    if not node.value:
      # vm.py preprocesses the source code so that all annotations in function
      # bodies have values. So the only annotations without values are module-
      # and class-level ones, which generate STORE opcodes and therefore
      # don't need to be handled here.
      return
    annotation = ast.unparse(node.annotation)
    if isinstance(node.target, ast.Name):
      name = node.target.id
    else:
      name = None
    self.variable_annotations.append(
        _VariableAnnotation(node.lineno, node.end_lineno, name, annotation)
    )
    self._process_structured_comments(LineRange.from_node(node))

  def _visit_try(self, node):
    for handler in node.handlers:
      if handler.type:
        self._process_structured_comments(LineRange.from_node(handler.type))

  def visit_Try(self, node):
    self._visit_try(node)

  def visit_TryStar(self, node):
    self._visit_try(node)

  def _visit_with(self, node):
    item = node.items[-1]
    end_lineno = (item.optional_vars or item.context_expr).end_lineno
    if self.block_depth == 1:
      self.block_returns.add_block(node)
    self._process_structured_comments(LineRange(node.lineno, end_lineno))

  def enter_With(self, node):
    self.block_depth += 1

  def leave_With(self, node):
    self.block_depth -= 1

  def enter_AsyncWith(self, node):
    self.block_depth += 1

  def leave_AsyncWith(self, node):
    self.block_depth -= 1

  def visit_With(self, node):
    self._visit_with(node)

  def visit_AsyncWith(self, node):
    self._visit_with(node)

  def _is_underscore(self, node):
    """Check if a match case is the default `case _`."""
    if node.pattern is None:
      return True
    elif not isinstance(node.pattern, ast.MatchAs):
      return False
    else:
      return self._is_underscore(node.pattern)

  def visit_Match(self, node):
    start = node.lineno
    end = node.end_lineno
    cases = []
    for c in node.cases:
      if isinstance(c.pattern, ast.MatchAs):
        name = c.pattern and c.pattern.name
      else:
        name = None
      is_underscore = self._is_underscore(c)
      match_case = _MatchCase(
          start=c.pattern.lineno,
          end=c.pattern.end_lineno,
          as_name=name,
          is_underscore=is_underscore,
          match_line=start,
      )
      cases.append(match_case)
    self.matches.add_match(start, end, cases)

  def generic_visit(self, node):
    if not isinstance(node, ast.stmt):
      return
    if hasattr(node, "body"):
      # For something like an If node, we need to add a range spanning the
      # header (the `if ...:` part).
      prev_field_value = None
      end_lineno = None
      for field_name, field_value in ast.iter_fields(node):
        if field_name == "body":
          assert prev_field_value
          end_lineno = prev_field_value.end_lineno
          break
        prev_field_value = field_value
      assert end_lineno  # this should always be set here
      self._process_structured_comments(LineRange(node.lineno, end_lineno))
    else:
      self._process_structured_comments(LineRange.from_node(node))

  def visit_Return(self, node):
    self.block_returns.add_return(node)
    self._process_structured_comments(LineRange.from_node(node))

  def _visit_decorators(self, node):
    if not node.decorator_list:
      return
    for dec in node.decorator_list:
      self._process_structured_comments(LineRange.from_node(dec))
      dec_base = dec.func if isinstance(dec, ast.Call) else dec
      self.decorators[node.lineno].append((dec.lineno, ast.unparse(dec_base)))

  def _visit_def(self, node):
    self._visit_decorators(node)
    if not self.defs_start or node.lineno < self.defs_start:
      self.defs_start = node.lineno

  def visit_ClassDef(self, node):
    self._visit_def(node)

  def _visit_function_def(self, node):
    # A function signature's line range should start at the beginning of the
    # signature and end at the final colon. Since we can't get the lineno of
    # the final colon from the ast, we do our best to approximate it.
    start_lineno = node.lineno
    # Find the last line that is definitely part of the function signature.
    if node.returns:
      maybe_end_lineno = node.returns.end_lineno
    elif node.args.args:
      maybe_end_lineno = node.args.args[-1].end_lineno
    else:
      maybe_end_lineno = start_lineno
    # Find the first line that is definitely not part of the function signature.
    body_lineno = node.body[0].lineno
    if body_lineno <= maybe_end_lineno:
      # The end of the signature and the start of the body are on the same line.
      end_lineno = maybe_end_lineno
    else:
      for i in range(maybe_end_lineno, body_lineno):
        if any(
            c.tool == "type" and c.open_ended
            for c in self._raw_structured_comments[i]
        ):
          # If we find a function type comment, the end of the signature is the
          # line before the type comment.
          end_lineno = i - 1
          break
      else:
        # Otherwise, the signature ends on the line before the body starts.
        end_lineno = body_lineno - 1
    self._process_structured_comments(LineRange(start_lineno, end_lineno))
    self._visit_def(node)
    # The function range starts at the start of the first decorator (we use it
    # to see if a line is within a function).
    if node.decorator_list:
      start_lineno = min(d.lineno for d in node.decorator_list)
    self.function_ranges[start_lineno] = node.end_lineno
    # Record all the `name: type` annotations in the signature
    args = node.args.posonlyargs + node.args.args + node.args.kwonlyargs
    annots = {
        arg.arg: ast.unparse(arg.annotation) for arg in args if arg.annotation
    }
    self.param_annotations.append(
        _ParamAnnotations(node.lineno, node.end_lineno, node.name, annots)
    )

  def visit_FunctionDef(self, node):
    self._visit_function_def(node)

  def visit_AsyncFunctionDef(self, node):
    self._visit_function_def(node)


def _process_comments(src):
  structured_comments = collections.defaultdict(list)
  f = io.StringIO(src)
  for token in tokenize.generate_tokens(f.readline):
    tok = token.exact_type
    line = token.line
    lineno, col = token.start
    if tok == tokenize.COMMENT:
      structured_comments[lineno].extend(_process_comment(line, lineno, col))
  return structured_comments


def _process_comment(line, lineno, col):
  """Process a single comment."""
  matches = list(_DIRECTIVE_RE.finditer(line[col:]))
  if not matches:
    return
  open_ended = not line[:col].strip()
  is_nested = matches[0].start(0) > 0
  for m in matches:
    tool, data = m.groups()
    assert data is not None
    data = data.strip()
    if tool == "pytype" and data == "skip-file":
      # Abort immediately to avoid unnecessary processing.
      raise SkipFileError()
    if tool == "type" and open_ended and is_nested:
      # Discard type comments embedded in larger whole-line comments.
      continue
    yield _StructuredComment(lineno, tool, data, open_ended)


def parse_src(src: str, python_version: tuple[int, int]):
  """Parses a string of source code into an ast."""
  return _SourceTree(
      ast.parse(src, feature_version=python_version[1]), _process_comments(src)
  )  # pylint: disable=unexpected-keyword-arg


def visit_src_tree(src_tree):
  parse_visitor = _ParseVisitor(src_tree.structured_comments)
  parse_visitor.visit(src_tree.ast)
  return parse_visitor
