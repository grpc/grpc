"""Debugging utilities."""

# pylint: disable=protected-access


def dump(node, ast, annotate_fields=True, include_attributes=True, indent="  "):
  """Return a formatted dump of the tree in *node*.

  This is mainly useful for debugging purposes.  The returned string will show
  the names and the values for fields.  This makes the code impossible to
  evaluate, so if evaluation is wanted *annotate_fields* must be set to False.
  Attributes such as line numbers and column offsets are dumped by default. If
  this is not wanted, *include_attributes* can be set to False.

  Arguments:
    node: Top AST node.
    ast: An module providing an AST class hierarchy.
    annotate_fields: Show field annotations.
    include_attributes: Show all attributes.
    indent: Indentation string.

  Returns:
    A formatted tree.
  """
  # Code copied from:
  # http://alexleone.blogspot.com/2010/01/python-ast-pretty-printer.html

  def _format(node, level=0):
    """Format a subtree."""

    if isinstance(node, ast.AST):
      fields = [(a, _format(b, level)) for a, b in ast.iter_fields(node)]
      if include_attributes and node._attributes:
        fields.extend(
            [(a, _format(getattr(node, a), level)) for a in node._attributes]
        )
      return "".join([
          node.__class__.__name__,
          "(",
          ", ".join(
              ("%s=%s" % field for field in fields)
              if annotate_fields
              else (b for a, b in fields)
          ),
          ")",
      ])
    elif isinstance(node, list):
      lines = ["["]
      lines.extend(
          indent * (level + 2) + _format(x, level + 2) + "," for x in node
      )
      if len(lines) > 1:
        lines.append(indent * (level + 1) + "]")
      else:
        lines[-1] += "]"
      return "\n".join(lines)
    return repr(node)

  if not isinstance(node, ast.AST):
    raise TypeError(f"expected AST, got {node.__class__!r}")
  return _format(node)
