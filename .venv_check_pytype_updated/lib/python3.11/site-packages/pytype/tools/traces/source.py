"""Source and trace information."""

import collections
import dataclasses
from typing import Any, NamedTuple

from pytype.pytd import pytd


class Location(NamedTuple):
  line: int
  column: int


@dataclasses.dataclass(eq=True, frozen=True)
class AbstractTrace:
  """Base class for traces."""
  op: str
  symbol: Any
  types: tuple[pytd.Node, ...]

  def __new__(cls, op, symbol, types):
    del op, symbol, types  # unused
    if cls is AbstractTrace:
      raise TypeError("cannot instantiate AbstractTrace")
    return super().__new__(cls)

  def __repr__(self):
    return f"{self.op} : {self.symbol} <- {self.types}"


class Code:
  """Line-based source code access.

  Attributes:
    text: The source text.
    traces: A dictionary from line number to traces.
    filename: The filename - when using traces.trace(), this value is meaningful
      only if an options object containing the filename was provided.
  """

  def __init__(self, src, raw_traces, trace_factory, filename):
    """Initializer.

    Args:
      src: The source text.
      raw_traces: Raw (opcode, symbol, types) values.
      trace_factory: A subclass of source.AbstractTrace that will be used to
        instantiate traces from raw values.
      filename: The filename.
    """
    self.text = src
    self.traces = _collect_traces(raw_traces, trace_factory)
    self.filename = filename
    self._lines = src.split("\n")
    self._offsets = []
    self._init_byte_offsets()

  def _init_byte_offsets(self):
    offset = 0
    for line in self._lines:
      self._offsets.append(offset)
      # convert line to bytes
      bytes_ = line.encode("utf-8")
      offset += len(bytes_) + 1  # account for the \n

  def get_offset(self, location):
    """Gets the utf-8 byte offset of a source.Location from start of source."""
    return self._offsets[location.line - 1] + location.column

  def line(self, n):
    """Gets the text at a line number."""
    return self._lines[n - 1]

  def get_closest_line_range(self, start, end):
    """Gets all valid line numbers in the [start, end) line range."""
    return range(start, min(end, len(self._lines) + 1))

  def find_first_text(self, start, end, text):
    """Gets first location, if any, the string appears at in the line range."""

    for l in self.get_closest_line_range(start, end):
      col = self.line(l).find(text)
      if col > -1:
        # TODO(mdemello): Temporary hack, replace with a token stream!
        # This will break if we have a # in a string before our desired text.
        comment_marker = self.line(l).find("#")
        if -1 < comment_marker < col:
          continue
        return Location(l, col)
    return None

  def next_non_comment_line(self, line):
    """Gets the next non-comment line, if any, after the given line."""
    for l in range(line + 1, len(self._lines) + 1):
      if self.line(l).lstrip().startswith("#"):
        continue
      return l
    return None

  def display_traces(self):
    """Prints the source file with traces for debugging."""
    for line in sorted(self.traces):
      print("%d %s" % (line, self.line(line)))
      for trace in self.traces[line]:
        print(f"  {trace}")
      print("-------------------")

  def get_attr_location(self, name, location):
    """Returns the location and span of the attribute in an attribute access.

    Args:
      name: The attribute name.
      location: The location of the value the attribute is accessed on.
    """
    # TODO(mdemello): This is pretty crude, and does not for example take into
    # account multiple calls of the same attribute in a line. It is just to get
    # our tests passing until we incorporate asttokens.
    line, _ = location
    src_line = self.line(line)
    attr = name.split(".")[-1]
    dot_attr = "." + attr
    if dot_attr in src_line:
      col = src_line.index(dot_attr)
      return (Location(line, col + 1), len(attr))
    else:
      # We have something like
      #   (foo
      #      .bar)
      # or
      #   (foo.
      #     bar)
      # Lookahead up to 5 lines to find '.attr' (the ast node always starts from
      # the beginning of the chain, so foo.\nbar.\nbaz etc could span several
      # lines).
      attr_loc = self._get_multiline_location(location, 5, dot_attr)
      if attr_loc:
        return (Location(attr_loc.line, attr_loc.column + 1), len(attr))
      else:
        # Find consecutive lines ending with '.' and starting with 'attr'.
        for l in self.get_closest_line_range(line, line + 5):
          if self.line(l).endswith("."):
            next_line = self.next_non_comment_line(l)
            text = self.line(next_line)
            if text.lstrip().startswith(attr):
              c = text.index(attr)
              return (Location(next_line, c), len(attr))
      # if all else fails, fall back to just spanning the name
      return (location, len(name))

  def _get_multiline_location(self, location, n_lines, text):
    """Gets the start location of text anywhere within n_lines of location."""
    line, _ = location
    text_loc = self.find_first_text(line, line + n_lines, text)
    if text_loc:
      return text_loc
    else:
      return None


def _collect_traces(raw_traces, trace_factory):
  """Postprocesses pytype's opcode traces."""
  out = collections.defaultdict(list)
  for op, symbol, data in raw_traces:
    out[op.line].append(trace_factory(op.name, symbol, data))
  return out
