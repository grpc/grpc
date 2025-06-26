"""Evaluate subtrees corresponding to python literals.

This is a modified copy of typed_ast.ast3.literal_eval. The latter doesn't
handle Name nodes, so it would not handle something like "{'type': A}". Our
version converts that to "{'type': 'A'}" which is consistent with
auto-stringifying type annotations.

We also separate out string and node evaluation into separate functions.
"""

import ast as astlib
from pytype.pyi import types


_NUM_TYPES = (int, float, complex)


# pylint: disable=invalid-unary-operand-type
def _convert(node):
  """Helper function for literal_eval."""
  if isinstance(node, astlib.Constant):
    return node.value
  elif isinstance(node, astlib.Tuple):
    return tuple(map(_convert, node.elts))
  elif isinstance(node, astlib.List):
    return list(map(_convert, node.elts))
  elif isinstance(node, astlib.Set):
    return set(map(_convert, node.elts))
  elif isinstance(node, astlib.Dict):
    return {_convert(k): _convert(v) for k, v in zip(node.keys, node.values)}
  elif isinstance(node, astlib.Name):
    return node.id
  elif isinstance(node, types.Pyval):
    return node.value
  elif node.__class__.__name__ == "NamedType" and node.name == "None":
    # We convert None to pytd.NamedType('None') in types.Pyval
    return None
  elif isinstance(node, astlib.UnaryOp) and isinstance(
      node.op, (astlib.UAdd, astlib.USub)
  ):
    operand = _convert(node.operand)
    if isinstance(operand, _NUM_TYPES):
      if isinstance(node.op, astlib.UAdd):
        return operand
      else:
        return -operand
  elif isinstance(node, astlib.BinOp) and isinstance(
      node.op, (astlib.Add, astlib.Sub)
  ):
    left = _convert(node.left)
    right = _convert(node.right)
    if isinstance(left, _NUM_TYPES) and isinstance(right, _NUM_TYPES):
      if isinstance(node.op, astlib.Add):
        return left + right
      else:
        return left - right
  raise ValueError("Cannot evaluate node: " + repr(node))


# pylint: enable=invalid-unary-operand-type


def literal_eval(node):
  """Modified version of ast.literal_eval, handling things like typenames."""
  if isinstance(node, astlib.Expression):
    node = node.body
  if isinstance(node, astlib.Expr):
    node = node.value
  return _convert(node)


def eval_string_literal(src: str):
  return literal_eval(astlib.parse(src, mode="eval"))
