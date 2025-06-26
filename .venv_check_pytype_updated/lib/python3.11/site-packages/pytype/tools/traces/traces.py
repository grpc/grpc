"""A library for accessing pytype's inferred local types."""

import itertools
import re

from pytype import analyze
from pytype import config
from pytype import load_pytd
from pytype.ast import visitor
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors

from pytype.tools.traces import source

_ATTR_OPS = frozenset((
    "LOAD_ATTR",
    "LOAD_METHOD",
    "STORE_ATTR",
))

_BINMOD_OPS = frozenset((
    "BINARY_MODULO",
    "BINARY_OP",
    "FORMAT_VALUE",
))

_CALL_OPS = frozenset((
    "CALL",
    "CALL_FUNCTION",
    "CALL_FUNCTION_EX",
    "CALL_FUNCTION_KW",
    "CALL_FUNCTION_VAR",
    "CALL_FUNCTION_VAR_KW",
    "CALL_METHOD",
))

_LOAD_OPS = frozenset((
    "LOAD_DEREF",
    "LOAD_FAST",
    "LOAD_GLOBAL",
    "LOAD_NAME",
))

_LOAD_SUBSCR_METHODS = ("__getitem__", "__getslice__")
_LOAD_SUBSCR_OPS = frozenset((
    "BINARY_SLICE",
    "BINARY_SUBSCR",
    "SLICE_0",
    "SLICE_1",
    "SLICE_2",
    "SLICE_3",
))

_STORE_OPS = frozenset((
    "STORE_DEREF",
    "STORE_FAST",
    "STORE_GLOBAL",
    "STORE_NAME",
))


class TypeTrace(source.AbstractTrace):
  """Traces of inferred type information."""


def trace(src, options=None):
  """Generates type traces for the given source code.

  Args:
    src: The source text.
    options: A pytype.config.Options object that can be used to specify options
      such as the target Python version.

  Returns:
    A source.Code object.
  """
  options = options or config.Options.create()
  with config.verbosity_from(options):
    loader = load_pytd.create_loader(options)
    ret = analyze.infer_types(
        src=src,
        options=options,
        loader=loader)
    pytd_module = ret.ast
    raw_traces = []
    for op, symbol, data in ret.context.vm.opcode_traces:
      raw_traces.append(
          (op, symbol, tuple(_to_pytd(d, loader, pytd_module) for d in data)))
  return source.Code(src, raw_traces, TypeTrace, options.input)


def _to_pytd(datum, loader, ast):
  if not datum:
    return pytd.AnythingType()
  t = pytd_utils.JoinTypes(v.to_pytd_type() for v in datum).Visit(
      visitors.RemoveUnknownClasses())
  return loader.resolve_pytd(t, ast)


class _SymbolMatcher:
  """Symbol matcher for MatchAstVisitor._get_traces.

  Allows matching against:
    - a regular expression (will use re.match)
    - an arbitrary object (will use object equality)
    - a tuple of the above (will match if any member does)
  """

  @classmethod
  def from_one_match(cls, match):
    return cls((match,))

  @classmethod
  def from_tuple(cls, matches):
    return cls(matches)

  @classmethod
  def from_regex(cls, regex):
    return cls((re.compile(regex),))

  def __init__(self, matches):
    self._matches = matches

  def match(self, symbol):
    for match in self._matches:
      if isinstance(match, re.Pattern):
        if match.match(str(symbol)):
          return True
      elif match == symbol:
        return True
    return False


class MatchAstVisitor(visitor.BaseVisitor):
  """An AST visitor to match traces to nodes.

  Attributes:
    source: The source and trace information.
  """

  def __init__(self, src_code, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.source = src_code
    # Needed for x[i] = <multiline statement>
    self._assign_subscr = None
    # For tracking already matched traces
    self._matched = None

  def enter_Assign(self, node):
    if isinstance(node.targets[0], self._ast.Subscript):
      self._assign_subscr = node.targets[0].value

  def leave_Assign(self, _):
    self._assign_subscr = None

  def enter_Module(self, _):
    self._matched = set()

  def leave_Module(self, _):
    self._matched = None

  def match(self, node):
    """Gets the traces for the given node, along with their locations."""
    method = "match_" + node.__class__.__name__
    try:
      match = getattr(self, method)
    except AttributeError as e:
      raise NotImplementedError(method) from e
    return match(node)

  def match_Attribute(self, node):
    if hasattr(node, "end_lineno"):
      n = node.end_lineno - node.lineno + 1
    else:
      n = 1
    trs = self._get_traces(
        node.lineno, _ATTR_OPS, node.attr, maxmatch=1, num_lines=n)
    return [(self._get_match_location(node, tr.symbol), tr)
            for tr in trs]

  def match_BinOp(self, node):
    if not isinstance(node.op, self._ast.Mod):
      raise NotImplementedError(f"match_Binop:{node.op.__class__.__name__}")
    symbol = "__mod__"
    # The node's lineno is the first line of the operation, but the opcode's
    # lineno is the last line, so we look ahead to try to find the last line.
    # We do a long lookahead in order to support formatting of long strings.
    return [(self._get_match_location(node), tr) for tr in self._get_traces(
        node.lineno, _BINMOD_OPS, symbol, maxmatch=1, num_lines=10)]

  def match_Call(self, node):
    # When calling a method of a class, the node name is <value>.<method>, but
    # only the method name is traced.
    name = self._get_node_name(node).rpartition(".")[-1]
    # The node's lineno is the first line of the call, but the opcode's lineno
    # is the last line, so we look ahead to try to find the last line.
    return [(self._get_match_location(node), tr)
            for tr in self._get_traces(
                node.lineno, _CALL_OPS, name, maxmatch=1, num_lines=5)]

  def match_Constant(self, node):
    # As of Python 3.8, bools, numbers, bytes, strings, ellipsis etc are
    # all constants instead of individual ast nodes.
    return self._match_constant(node, node.value)

  def match_FunctionDef(self, node):
    symbol = _SymbolMatcher.from_regex(r"(%s|None)" % node.name)
    return [
        (self._get_match_location(node, tr.symbol), tr)
        for tr in self._get_traces(node.lineno, ["MAKE_FUNCTION"], symbol, 1)
    ]

  def match_Import(self, node):
    return list(self._match_import(node, is_from=False))

  def match_ImportFrom(self, node):
    return list(self._match_import(node, is_from=True))

  def match_Lambda(self, node):
    sym = _SymbolMatcher.from_regex(r".*<lambda>$")
    return [(self._get_match_location(node), tr)
            for tr in self._get_traces(node.lineno, ["MAKE_FUNCTION"], sym, 1)]

  def match_Name(self, node):
    if isinstance(node.ctx, self._ast.Load):
      lineno = node.lineno
      ops = _LOAD_OPS
    elif isinstance(node.ctx, self._ast.Store):
      lineno = node.lineno
      ops = _STORE_OPS
    else:
      return []
    return [(self._get_match_location(node), tr)
            for tr in self._get_traces(lineno, ops, node.id, 1)]

  def match_Subscript(self, node):
    return [(self._get_match_location(node), tr) for tr in self._get_traces(
        node.lineno, _LOAD_SUBSCR_OPS,
        _SymbolMatcher.from_tuple(_LOAD_SUBSCR_METHODS), 1)]

  def _get_traces(self, lineno, ops, symbol, maxmatch=-1, num_lines=1):
    """Yields matching traces.

    Args:
      lineno: A starting line number.
      ops: A list of opcode names to match on.
      symbol: A symbol or _SymbolMatcher instance to match on.
      maxmatch: The maximum number of traces to yield. -1 for no maximum.
      num_lines: The number of consecutive lines to search.
    """
    if not isinstance(symbol, _SymbolMatcher):
      symbol = _SymbolMatcher.from_one_match(symbol)
    for tr in itertools.chain.from_iterable(
        self.source.traces[line] for line in range(lineno, lineno + num_lines)):
      if maxmatch == 0:
        break
      m_matched = self._matched
      assert m_matched is not None
      if (id(tr) not in m_matched and tr.op in ops and symbol.match(tr.symbol)):
        maxmatch -= 1
        m_matched.add(id(tr))
        yield tr

  def _get_match_location(self, node, name=None):
    loc = source.Location(node.lineno, node.col_offset)
    if not name:
      return loc
    if isinstance(node, (self._ast.Import, self._ast.ImportFrom)):
      # Search for imported module names
      m = re.search("[ ,]" + name + r"\b", self.source.line(node.lineno))
      if m is not None:
        c, _ = m.span()
        return source.Location(node.lineno, c + 1)
    elif isinstance(node, self._ast.Attribute):
      attr_loc, _ = self.source.get_attr_location(name, loc)
      return attr_loc
    return loc

  def _get_node_name(self, node):
    if isinstance(node, self._ast.Attribute):
      return f"{self._get_node_name(node.value)}.{node.attr}"
    elif isinstance(node, self._ast.Call):
      return self._get_node_name(node.func)
    elif isinstance(node, self._ast.Lambda):
      return "<lambda>"
    elif isinstance(node, self._ast.Name):
      return node.id
    else:
      return node.__class__.__name__

  def _match_constant(self, node, value):
    return [(self._get_match_location(node), tr)
            for tr in self._get_traces(node.lineno, ["LOAD_CONST"], value, 1)]

  def _match_import(self, node, is_from):
    for alias in node.names:
      name = alias.asname if alias.asname else alias.name
      op = "STORE_NAME" if alias.asname or is_from else "IMPORT_NAME"
      for tr in self._get_traces(node.lineno, [op], name, 1):
        yield self._get_match_location(node, name), tr


class _LineNumberVisitor(visitor.BaseVisitor):

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.line = 0

  def generic_visit(self, node):
    lineno = getattr(node, "lineno", 0)
    if lineno > self.line:
      self.line = lineno
