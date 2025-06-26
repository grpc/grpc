"""Process conditional blocks in pyi files."""

import ast as astlib

from pytype.ast import visitor as ast_visitor
from pytype.pyi import types
from pytype.pytd import slots as cmp_slots

_ParseError = types.ParseError


class ConditionEvaluator(ast_visitor.BaseVisitor):
  """Evaluates if statements in pyi files."""

  def __init__(self, options):
    super().__init__(ast=astlib)
    self._compares = {
        astlib.Eq: cmp_slots.EQ,
        astlib.Gt: cmp_slots.GT,
        astlib.Lt: cmp_slots.LT,
        astlib.GtE: cmp_slots.GE,
        astlib.LtE: cmp_slots.LE,
        astlib.NotEq: cmp_slots.NE,
    }
    self._options = options

  def _eval_comparison(
      self,
      ident: tuple[str, int | slice | None],
      op: str,
      value: str | int | tuple[int, ...],
  ) -> bool:
    """Evaluate a comparison and return a bool.

    Args:
      ident: A tuple of a dotted name string and an optional __getitem__ key.
      op: One of the comparison operator strings in cmp_slots.COMPARES.
      value: The value to be compared against.

    Returns:
      The boolean result of the comparison.

    Raises:
      ParseError: If the comparison cannot be evaluated.
    """
    name, key = ident
    if name == "sys.version_info":
      if key is None:
        key = slice(None, None, None)
      if isinstance(key, int) and not isinstance(value, int):
        raise _ParseError(
            "an element of sys.version_info must be compared to an integer"
        )
      if isinstance(key, slice) and not _is_int_tuple(value):
        raise _ParseError(
            "sys.version_info must be compared to a tuple of integers"
        )
      try:
        actual = self._options.python_version[key]
      except IndexError as e:
        raise _ParseError(str(e)) from e
      if isinstance(key, slice):
        actual = _three_tuple(actual)
        value = _three_tuple(value)
    elif name == "sys.platform":
      if not isinstance(value, str):
        raise _ParseError("sys.platform must be compared to a string")
      valid_cmps = (cmp_slots.EQ, cmp_slots.NE)
      if op not in valid_cmps:
        raise _ParseError(
            "sys.platform must be compared using %s or %s" % valid_cmps
        )
      actual = self._options.platform
    else:
      raise _ParseError(f"Unsupported condition: {name!r}.")
    return cmp_slots.COMPARES[op](actual, value)

  def fail(self, name=None):
    if name:
      msg = f"Unsupported condition: {name!r}. "
    else:
      msg = "Unsupported condition. "
    msg += "Supported checks are sys.platform and sys.version_info"
    raise _ParseError(msg)

  def visit_Attribute(self, node):
    if not isinstance(node.value, astlib.Name):
      self.fail()
    name = f"{node.value.id}.{node.attr}"
    if node.value.id == "sys":
      return name
    elif node.value.id == "PYTYPE_OPTIONS":
      if hasattr(self._options, node.attr):
        return bool(getattr(self._options, node.attr))
    self.fail(name)

  def visit_Slice(self, node):
    return slice(node.lower, node.upper, node.step)

  def visit_Index(self, node):
    return node.value

  def visit_Pyval(self, node):
    return node.value

  def visit_Subscript(self, node):
    return (node.value, node.slice)

  def visit_Tuple(self, node):
    return tuple(node.elts)

  def visit_BoolOp(self, node):
    if isinstance(node.op, astlib.Or):
      return any(node.values)
    elif isinstance(node.op, astlib.And):
      return all(node.values)
    else:
      raise _ParseError(f"Unexpected boolean operator: {node.op}")

  def visit_UnaryOp(self, node):
    if isinstance(node.op, astlib.USub) and isinstance(node.operand, int):
      return -node.operand
    else:
      raise _ParseError(f"Unexpected unary operator: {node.op}")

  def visit_Compare(self, node):
    if isinstance(node.left, tuple):
      ident = node.left
    else:
      ident = (node.left, None)
    op = self._compares[type(node.ops[0])]
    right = node.comparators[0]
    return self._eval_comparison(ident, op, right)


def evaluate(test: astlib.AST, options) -> bool:
  return ConditionEvaluator(options).visit(test)


def _is_int_tuple(value):
  """Return whether the value is a tuple of integers."""
  return isinstance(value, tuple) and all(isinstance(v, int) for v in value)


def _three_tuple(value):
  """Append zeros and slice to normalize the tuple to a three-tuple."""
  return (value + (0, 0))[:3]
