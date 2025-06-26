"""Utility functions for ast nodes."""


def typename(node):
  return node.__class__.__name__


def get_name(node, ast):
  """Nodes have different name attributes."""

  if isinstance(node, ast.Attribute):
    return get_name(node.value, ast) + "." + node.attr
  elif isinstance(node, ast.arg):
    return node.arg
  elif isinstance(node, str):
    return node
  elif hasattr(node, "name"):
    return node.name
  elif hasattr(node, "id"):
    return node.id
  else:
    return "[" + typename(node) + "]"
