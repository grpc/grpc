"""Functions and visitors for transforming pytd."""

from pytype.pytd import optimize
from pytype.pytd import visitors


def RemoveMutableParameters(ast):
  """Change all mutable parameters in a pytd AST to a non-mutable form."""
  ast = ast.Visit(optimize.AbsorbMutableParameters())
  ast = ast.Visit(optimize.CombineContainers())
  ast = ast.Visit(optimize.MergeTypeParameters())
  ast = ast.Visit(visitors.AdjustSelf(force=True))
  return ast
