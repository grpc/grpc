"""Base visitor for ast parse trees."""

import ast as astlib

from pytype.ast import visitor as ast_visitor
from pytype.pyi import types

_ParseError = types.ParseError


class BaseVisitor(ast_visitor.BaseVisitor):
  """Base visitor for all ast visitors.

  - Reraises ParseError with position information.
  - Handles literal constants
  - Has an optional Definitions member
  """

  def __init__(self, *, filename=None, src_code=None, visit_decorators=False):
    super().__init__(astlib, visit_decorators=visit_decorators)
    self.filename = filename  # used for error messages
    self.src_code = src_code  # used for error messages

  def enter(self, node):
    try:
      return super().enter(node)
    except Exception as e:  # pylint: disable=broad-except
      raise _ParseError.from_exc(e).at(node, self.filename, self.src_code)

  def visit(self, node):
    try:
      return super().visit(node)
    except Exception as e:  # pylint: disable=broad-except
      raise _ParseError.from_exc(e).at(node, self.filename, self.src_code)

  def leave(self, node):
    try:
      return super().leave(node)
    except Exception as e:  # pylint: disable=broad-except
      raise _ParseError.from_exc(e).at(node, self.filename, self.src_code)

  def generic_visit(self, node):
    return node
