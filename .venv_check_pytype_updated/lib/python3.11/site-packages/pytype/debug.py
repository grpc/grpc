"""Debugging helper functions."""

import collections
import contextlib
import inspect
import io
import logging
import re
import traceback

from pytype import utils
from pytype.typegraph import cfg_utils
import tabulate


def _ascii_tree(out, node, p1, p2, seen, get_children, get_description=None):
  """Draw a graph, starting at a given position.

  Args:
    out: A file-like object to write the ascii tree to.
    node: The node from where to draw.
    p1: The current prefix.
    p2: The upcoming prefix.
    seen: Nodes we have seen so far (as a set).
    get_children: The function to call to retrieve children.
    get_description: Optional. A function to call to describe a node.
  """
  children = list(get_children(node))
  text = get_description(node) if get_description else str(node)
  if node in seen:
    out.write(p1 + "[" + text + "]\n")
  else:
    out.write(p1 + text + "\n")
    seen.add(node)
    for i, c in enumerate(children):
      last = i == len(children) - 1
      out.write(p2 + "|\n")
      _ascii_tree(
          out,
          c,
          p2 + "+-",
          p2 + ("  " if last else "| "),
          seen,
          get_children,
          get_description,
      )


def ascii_tree(node, get_children, get_description=None):
  """Draw a graph, starting at a given position.

  Args:
    node: The node from where to draw.
    get_children: The function to call to retrieve children.
    get_description: Optional. A function to call to describe a node.

  Returns:
    A string.
  """
  out = io.StringIO()
  _ascii_tree(out, node, "", "", set(), get_children, get_description)
  return out.getvalue()


def prettyprint_binding(binding, indent_level=0):
  """Pretty print a binding with variable id and data."""
  indent = " " * indent_level
  if not binding:
    return indent + "<>"
  return "%s<v%d : %r>" % (indent, binding.variable.id, binding.data)


def prettyprint_binding_set(binding_set, indent_level=0, label=""):
  """Pretty print a set of bindings, with optional label."""
  indent = " " * indent_level
  start = f"{indent}{label}: {{"
  if not binding_set:
    return start + " }"
  return "\n".join(
      [start]
      + [prettyprint_binding(x, indent_level + 2) for x in binding_set]
      + [indent + "}"]
  )


def prettyprint_binding_nested(binding, indent_level=0):
  """Pretty print a binding and its recursive contents."""
  indent = " " * indent_level
  if indent_level > 32:
    return indent + "-[ max recursion depth exceeded ]-\n"
  s = f"{indent}binding v{binding.variable.id}={binding.data!r}\n"
  other = ""
  for v in binding.variable.bindings:
    if v is not binding:
      other += f"{v.data!r} {[o.where for o in v.origins]} "
  if other:
    s += f"{indent}(other assignments: {other})\n"
  for origin in binding.origins:
    s += f"{indent}  at {origin.where}\n"
    for i, source_set in enumerate(origin.source_sets):
      for j, source in enumerate(source_set):
        s += prettyprint_binding_nested(source, indent_level + 4)
        if j < len(source_set) - 1:
          s += f"{indent}    AND\n"
      if i < len(origin.source_sets) - 1:
        s += f"{indent}  OR\n"
  return s


def prettyprint_cfg_node(node, decorate_after_node=0, full=False):
  """A reasonably compact representation of all the bindings at a node.

  Args:
    node: The node to prettyprint.
    decorate_after_node: Don't print bindings unless node_id > this.
    full: Print the full string representation of a binding's data

  Returns:
    A prettyprinted node.
  """
  if node.id <= decorate_after_node:
    return repr(node) + f" [{len(node.bindings)} bindings]"
  if full:
    name = lambda x: getattr(x, "name", str(x))
  else:
    name = str
  bindings = collections.defaultdict(list)
  for b in node.bindings:
    bindings[b.variable.id].append(name(b.data))
  b = ", ".join(
      ["%d:%s" % (k, "|".join(v)) for k, v in sorted(bindings.items())]
  )
  return repr(node) + " [" + b + "]"


def prettyprint_cfg_tree(
    root, decorate_after_node=0, full=False, forward=False
):
  """Pretty print a cfg tree with the bindings at each node.

  Args:
    root: The root node.
    decorate_after_node: Don't print bindings unless node_id > this.
    full: Print the full string representation of a binding's data
    forward: Traverse the tree forwards if true.

  Returns:
    A prettyprinted tree.
  """
  if forward:
    children = lambda node: node.outgoing
  else:
    children = lambda node: node.incoming
  desc = lambda node: prettyprint_cfg_node(node, decorate_after_node, full)
  return ascii_tree(root, get_children=children, get_description=desc)


def _pretty_variable(var):
  """Return a pretty printed string for a Variable."""
  lines = []
  single_value = len(var.bindings) == 1
  var_desc = "v%d" % var.id
  if not single_value:
    # Write a description of the variable (for single value variables this
    # will be written along with the value later on).
    lines.append(var_desc)
    var_prefix = "  "
  else:
    var_prefix = var_desc + " = "

  for value in var.bindings:
    data = utils.maybe_truncate(value.data)
    binding = f"{var_prefix} {data}"

    if len(value.origins) == 1:
      # Single origin.  Use the binding as a prefix when writing the origin.
      prefix = binding + ", "
    else:
      # Multiple origins, write the binding on its own line, then indent all
      # of the origins.
      lines.append(binding)
      prefix = "    "

    for origin in value.origins:
      src = utils.pretty_dnf(
          [[str(v) for v in source_set] for source_set in origin.source_sets]
      )
      lines.append("%s%s @%d" % (prefix, src, origin.where.id))
  return "\n".join(lines)


def program_to_text(program):
  """Generate a text (CFG nodes + assignments) version of a program.

  For debugging only.

  Args:
    program: An instance of cfg.Program

  Returns:
    A string representing all of the data for this program.
  """

  def label(node):
    return "<%d>%s" % (node.id, node.name)

  s = io.StringIO()
  seen = set()
  for node in cfg_utils.order_nodes(program.cfg_nodes):
    seen.add(node)
    s.write(f"{label(node)}\n")
    s.write(f"  From: {', '.join(label(n) for n in node.incoming)}\n")
    s.write(f"  To: {', '.join(label(n) for n in node.outgoing)}\n")
    s.write("\n")
    variables = {value.variable for value in node.bindings}
    for var in sorted(variables, key=lambda v: v.id):
      # If a variable is bound in more than one node then it will be listed
      # redundantly multiple times.  One alternative would be to only list the
      # values that occur in the given node, and then also list the other nodes
      # that assign the same variable.

      # Write the variable, indenting by two spaces.
      s.write("  %s\n" % _pretty_variable(var).replace("\n", "\n  "))
    s.write("\n")

  return s.getvalue()


def root_cause(binding, node, seen=()):
  """Tries to determine why a binding isn't possible at a node.

  This tries to find the innermost source that's still impossible. It only works
  if the failure isn't due to a combination of bindings.

  Args:
    binding: A binding, or a list of bindings.
    node: The node at which (one of the) binding(s) is impossible.
    seen: Internal. Bindings already looked at.

  Returns:
    A tuple (binding, node), with "binding" the innermost binding that's
    not possible, and "node" the CFG node at which it isn't.
  """
  if isinstance(binding, (list, tuple)):
    bindings = list(binding)
  else:
    bindings = [binding]
  del binding
  key = frozenset(bindings)
  if key in seen:
    return next(iter(bindings), None), node
  for b in bindings:
    if not node.HasCombination([b]):
      for o in b.origins:
        for source_set in o.source_sets:
          cause, n = root_cause(list(source_set), o.where)
          if cause is not None:
            return cause, n
      return b, node
  return None, None


def stack_trace(indent_level=0, limit=100):
  indent = " " * indent_level
  stack = [
      frame
      for frame in traceback.extract_stack()
      if "/errors.py" not in frame[0] and "/debug.py" not in frame[0]
  ]
  tb = traceback.format_list(stack[-limit:])
  tb = [indent + re.sub(r"/usr/.*/pytype/", "", x) for x in tb]
  return "\n  ".join(tb)


def _setup_tabulate():
  """Customise tabulate."""
  tabulate.PRESERVE_WHITESPACE = True
  tabulate.MIN_PADDING = 0
  # Overwrite the 'presto' format to use the block-drawing vertical line.
  # pytype: disable=module-attr
  tabulate._table_formats["presto"] = tabulate.TableFormat(  # pylint: disable=protected-access
      lineabove=None,
      linebelowheader=tabulate.Line("", "-", "+", ""),
      linebetweenrows=None,
      linebelow=None,
      headerrow=tabulate.DataRow("", "│", ""),
      datarow=tabulate.DataRow("", "│", ""),
      padding=1,
      with_header_hide=None,
  )
  # pytype: enable=module-attr


def show_ordered_code(code, extra_col=None):
  """Print out the block structure of an OrderedCode object as a table.

  Args:
    code: A blocks.OrderedCode object
    extra_col: A map from opcode_index to a single additional cell to display
  """
  if not extra_col:
    extra_col = {}
  _setup_tabulate()
  block_lines = []
  op_lines = []
  boundaries = []
  start = 0
  for block in code.order:
    end = start
    ids = lambda xs: [x.id for x in xs]
    block_lines.append(
        f"block: {block.id} -> {ids(block.outgoing)} <- {ids(block.incoming)}"
    )
    for op in block:
      end += 1
      op_lines.append([
          op.index,
          op.__class__.__name__,
          getattr(op, "argval", ""),
          op.target and op.target.index,
          op.block_target and op.block_target.index,
          "✓" if op.push_exc_block else "",
          "✓" if op.pop_exc_block else "",
          op.next and op.next.index,
          op.line,
          extra_col.get(op.index),
      ])
    boundaries.append((start, end))
    start = end
  headers = [
      "ix",
      "op",
      "arg",
      "tgt",
      "btgt",
      ">exc",
      "<exc",
      "next",
      "line",
      "extra",
  ]
  block_table = tabulate.tabulate(op_lines, headers, tablefmt="presto")
  block_table = block_table.split("\n")
  tab = [[block_table[0]]]
  block_table = block_table[2:]
  for blk, (start, end) in zip(block_lines, boundaries):
    tab.append([blk])
    tab.append(["\n".join(block_table[start:end])])
  print(tabulate.tabulate(tab, tablefmt="fancy_grid"))


# Tracing logger
def tracer(name=None):
  name = f"trace.{name}" if name else "trace"
  return logging.getLogger(name)


def set_trace_level(level):
  logging.getLogger("trace").setLevel(level)


@contextlib.contextmanager
def tracing(level=logging.DEBUG):
  log = logging.getLogger("trace")
  current_level = log.getEffectiveLevel()
  log.setLevel(level)
  try:
    yield
  finally:
    log.setLevel(current_level)


def trace(name, *trace_args):
  """Record args and return value for a function call.

  The trace is of the form
    function name: {
    function name: arg = value
    function name: arg = value
    ...
    function name: -> return
    function name: }

  This will let us write tools to pretty print the traces with indentation etc.

  Args:
    name: module name, usually `__name__`
    *trace_args: function arguments to log

  Returns:
    a decorator
  """

  def decorator(f):
    def wrapper(*args, **kwargs):
      t = tracer(name)
      if t.getEffectiveLevel() < logging.DEBUG:
        return f(*args, **kwargs)
      argspec = inspect.getfullargspec(f)
      t.debug("%s: {", f.__name__)
      for arg in trace_args:
        if isinstance(arg, int):
          argname = argspec.args[arg]
          val = args[arg]
        else:
          argname = arg
          val = kwargs[arg]
        t.debug("%s: %s = %s", f.__name__, argname, show(val))
      ret = f(*args, **kwargs)
      t.debug("%s: -> %s", f.__name__, show(ret))
      t.debug("%s: }", f.__name__)
      return ret

    return wrapper

  return decorator


def show(x):
  """Pretty print values for debugging."""
  typename = x.__class__.__name__
  if typename == "Variable":
    return f"{x!r} {x.data}"
  else:
    return f"{x!r} <{typename}>"
