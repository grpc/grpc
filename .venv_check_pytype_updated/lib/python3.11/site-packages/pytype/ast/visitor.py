"""AST visiting."""


class BaseVisitor:
  """A base class for writing AST visitors.

  Subclasses should define {visit,enter,leave}_X to process nodes of type X.
  If a visit method returns a non-None value, the visited node is replaced
  with that value.

  Attributes:
    _ast: Any module whose interface matches the standard ast library, such as
      typed_ast. The same module must be used to generate the AST to visit.
  """

  def __init__(self, ast, visit_decorators=True):
    self._ast = ast
    maybe_decorators = ["decorator_list"] if visit_decorators else []
    self._node_children = {
        self._ast.Module: ["body"],
        self._ast.ClassDef: maybe_decorators + ["keywords", "bases", "body"],
        self._ast.FunctionDef: maybe_decorators + ["body", "args", "returns"],
        self._ast.Assign: ["targets", "value"],
    }

  def visit(self, node):
    """Does a post-order traversal of the AST."""
    if isinstance(node, self._ast.AST):
      self.enter(node)
      for k, v in self._children(node):
        ret = self.visit(v)
        if ret is not None:
          setattr(node, k, ret)
      out = self._call_visitor(node)
      self.leave(node)
      if out is not None:
        return out
    elif isinstance(node, list):
      for i, v in enumerate(node):
        ret = self.visit(v)
        if ret is not None:
          node[i] = ret

  def _children(self, node):
    """Children to recurse over."""
    ks = self._node_children.get(node.__class__)
    if ks:
      return [(k, getattr(node, k)) for k in ks]
    else:
      return self._ast.iter_fields(node)

  def _call_visitor(self, node):
    method = "visit_" + node.__class__.__name__
    visitor = getattr(self, method, self.generic_visit)
    return visitor(node)

  def generic_visit(self, node):
    """Called when no visit function is found for a node type."""
    del node  # unused

  def enter(self, node):
    """Does a pre-order traversal of the AST."""
    method = "enter_" + node.__class__.__name__
    visitor = getattr(self, method, None)
    if visitor:
      return visitor(node)

  def leave(self, node):
    """Called after visit() to do any cleanup that enter() needs."""
    method = "leave_" + node.__class__.__name__
    visitor = getattr(self, method, None)
    if visitor:
      visitor(node)
