"""Tests for traces.visitor."""

import ast
import sys
import textwrap
from pytype.ast import visitor
import unittest


class _VisitOrderVisitor(visitor.BaseVisitor):
  """Tests visit order."""

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.funcs = []

  def visit_FunctionDef(self, node):
    self.funcs.append(node.name)


class _VisitReplaceVisitor(visitor.BaseVisitor):
  """Tests visit()'s node replacement functionality."""

  def visit_Name(self, node):
    if node.id == "x":
      return True  # should replace
    elif node.id == "y":
      return False  # should replace
    else:
      return None  # should not replace


class _GenericVisitVisitor(visitor.BaseVisitor):
  """Tests generic_visit()."""

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.nodes = []

  def generic_visit(self, node):
    self.nodes.append(node.__class__.__name__)


class _EnterVisitor(visitor.BaseVisitor):
  """Tests enter() by recording names."""

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.names = []

  def enter_Name(self, node):
    self.names.append(node.id)


class _LeaveVisitor(_EnterVisitor):
  """Tests leave() by discarding names recorded by enter()."""

  def leave_Name(self, node):
    self.names.pop()


class custom_ast:  # pylint: disable=invalid-name
  """Tests a custom ast module."""

  class AST:
    pass

  class Thing(AST):
    pass

  def __getattr__(self, name):
    return type(name, (custom_ast.AST,), {})

  def iter_fields(self, node):
    if isinstance(node, custom_ast.Thing):
      return []
    elif isinstance(node, custom_ast.AST):
      return [("thing", node.thing)]

  def parse(self, unused_src):
    module = custom_ast.AST()
    module.thing = custom_ast.Thing()
    return module


class BaseVisitorTest(unittest.TestCase):
  """Tests for visitor.BaseVisitor."""

  def test_visit_order(self):
    module = ast.parse(textwrap.dedent("""
      def f():
        def g():
          def h():
            pass
    """))
    v = _VisitOrderVisitor(ast)
    v.visit(module)
    self.assertEqual(v.funcs, ["h", "g", "f"])

  def test_visit_replace(self):
    module = ast.parse(textwrap.dedent("""
      x.upper()
      y.upper()
      z.upper()
    """))
    v = _VisitReplaceVisitor(ast)
    v.visit(module)
    x = module.body[0].value.func.value
    y = module.body[1].value.func.value
    z = module.body[2].value.func.value
    self.assertIs(x, True)
    self.assertIs(y, False)
    self.assertIsInstance(z, ast.Name)

  def test_generic_visit(self):
    module = ast.parse("x = 0")
    v = _GenericVisitVisitor(ast)
    v.visit(module)
    #    Module
    #      |
    #    Assign
    #   /      \
    # Name     Constant (Num)
    #  |
    # Store

    # The "Num" ast class is deprecated as of Python 3.8 and ast.parse returns
    # "Constant" instead.
    if sys.hexversion >= 0x03080000:
      constant = "Constant"
    else:
      constant = "Num"

    self.assertEqual(v.nodes, ["Store", "Name", constant, "Assign", "Module"])

  def test_enter(self):
    module = ast.parse(textwrap.dedent("""
      x = 0
      y = 1
      z = 2
    """))
    v = _EnterVisitor(ast)
    v.visit(module)
    self.assertEqual(v.names, ["x", "y", "z"])

  def test_leave(self):
    module = ast.parse(textwrap.dedent("""
      x = 0
      y = 1
      z = 2
    """))
    v = _LeaveVisitor(ast)
    v.visit(module)
    self.assertFalse(v.names)

  def test_custom_ast(self):
    custom_ast_module = custom_ast()
    module = custom_ast_module.parse("")
    v = _GenericVisitVisitor(custom_ast_module)
    v.visit(module)
    self.assertEqual(v.nodes, ["Thing", "AST"])


if __name__ == "__main__":
  unittest.main()
