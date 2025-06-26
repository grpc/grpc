"""Preprocess source code before compilation."""

import ast


# pylint: disable=invalid-name
class CollectAnnotationLines(ast.NodeVisitor):
  """Collect line numbers of annotations to augment."""

  def __init__(self):
    self.annotation_lines = []
    self.in_function = False

  def visit_AnnAssign(self, node):
    if self.in_function and node.value is None:
      self.annotation_lines.append(node.end_lineno - 1)  # change to 0-based

  def visit_FunctionDef(self, node):
    self.in_function = True
    for n in node.body:
      self.visit(n)
    self.in_function = False


# pylint: enable=invalid-name


def augment_annotations(src):
  """Add an assignment to bare variable annotations."""
  try:
    tree = ast.parse(src)
  except SyntaxError:
    # Let the compiler catch and report this later.
    return src
  visitor = CollectAnnotationLines()
  visitor.visit(tree)
  if visitor.annotation_lines:
    lines = src.split("\n")
    for i in visitor.annotation_lines:
      # Preserve comments, as they may be pytype directives. We don't bother to
      # keep the formatting, since users never see the transformed source code.
      line, mark, comment = lines[i].partition("#")
      lines[i] = line + " = ..." + mark + comment
    src = "\n".join(lines)
  return src
