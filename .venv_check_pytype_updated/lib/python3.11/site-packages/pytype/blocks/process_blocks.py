"""Analyze code blocks and process opcodes."""

from pytype.blocks import blocks
from pytype.pyc import opcodes
from pytype.pyc import pyc


# Opcodes whose argument can be a block of code.
CODE_LOADING_OPCODES = (opcodes.LOAD_CONST,)


def _is_function_def(fn_code):
  """Helper function for CollectFunctionTypeCommentTargetsVisitor."""
  # Reject anything that is not a named function (e.g. <lambda>).
  first = fn_code.name[0]
  if not (first == "_" or first.isalpha()):
    return False

  # Class definitions generate a constructor function. We can distinguish them
  # by checking for code blocks that start with LOAD_NAME __name__
  op = fn_code.get_first_opcode()
  if isinstance(op, opcodes.LOAD_NAME) and op.argval == "__name__":
    return False

  return True


class CollectAnnotationTargetsVisitor(pyc.CodeVisitor):
  """Collect opcodes that might have annotations attached."""

  def __init__(self):
    super().__init__()
    # A mutable map of line: opcode for STORE_* opcodes. This is modified as the
    # visitor runs, and contains the last opcode for each line.
    self.store_ops = {}
    # A mutable map of start: (end, opcode) for MAKE_FUNCTION opcodes. This is
    # modified as the visitor runs, and contains the range of lines that could
    # contain function type comments.
    self.make_function_ops = {}

  def visit_code(self, code):
    """Find STORE_* and MAKE_FUNCTION opcodes for attaching annotations."""
    # Offset between function code and MAKE_FUNCTION
    # [LOAD_CONST <code>, LOAD_CONST <function name>, MAKE_FUNCTION]
    # In 3.11, the LOAD_CONST <function name> opcode is removed.
    offset = 1 if code.python_version >= (3, 11) else 2
    co_code = list(code.code_iter)
    for i, op in enumerate(co_code):
      if isinstance(op, opcodes.MAKE_FUNCTION):
        code_op = co_code[i - offset]
        assert isinstance(code_op, CODE_LOADING_OPCODES), code_op.__class__
        fn_code = code.consts[code_op.arg]
        if not _is_function_def(fn_code):
          continue
        # First line of code in body.
        end_line = min(
            op.line
            for op in fn_code.code_iter
            if not isinstance(op, opcodes.RESUME)
        )
        self.make_function_ops[op.line] = (end_line, op)
      elif (
          isinstance(op, blocks.STORE_OPCODES)
          and op.line not in self.make_function_ops
      ):
        # For type comments attached to multi-opcode lines, we want to mark the
        # latest 'store' opcode and attach the type comment to it.
        # Except that in 3.12+ list/dict/set comprehensions are inlined and end
        # with a STORE_FAST opcode, which set the iteration variable to NULL.
        # E.g.  `foo = [x for x in y]` ends with:
        #     END_FOR
        #     STORE_FAST foo
        #     STORE_FAST x
        # In this case we want to attach the comment to the 2nd to last opcode.
        #
        # Brittleness alert:
        # Taking the last opcode in a line is possibly confusing, e.g. for:
        #     a = ''; b = 1  # type: int
        # Matching comprehensions based on the 3 opcodes could probably also
        # fail. Feel free to adjust as necessary.
        if (
            code.python_version >= (3, 12)
            and i >= 2
            and isinstance(op, opcodes.STORE_FAST)
            and isinstance(co_code[i - 1], opcodes.STORE_FAST)
            and isinstance(co_code[i - 2], opcodes.END_FOR)
        ):
          continue
        self.store_ops[op.line] = op
    return code


class FunctionDefVisitor(pyc.CodeVisitor):
  """Add metadata to function definition opcodes."""

  def __init__(self, param_annotations):
    super().__init__()
    self.annots = param_annotations

  def visit_code(self, code):
    for op in code.code_iter:
      if isinstance(op, opcodes.MAKE_FUNCTION):
        if op.line in self.annots:
          op.metadata.signature_annotations = self.annots[op.line]
    return code


def merge_annotations(code, annotations, param_annotations):
  """Merges type comments into their associated opcodes.

  Modifies code in place.

  Args:
    code: An OrderedCode object.
    annotations: A map of lines to annotations.
    param_annotations: A list of _ParamAnnotations from the director

  Returns:
    The code with annotations added to the relevant opcodes.
  """
  if param_annotations:
    visitor = FunctionDefVisitor(param_annotations)
    pyc.visit(code, visitor)

  visitor = CollectAnnotationTargetsVisitor()
  code = pyc.visit(code, visitor)

  # Apply type comments to the STORE_* opcodes
  for line, op in visitor.store_ops.items():
    if line in annotations:
      annot = annotations[line]
      if annot.name in (None, op.argval):
        op.annotation = annot.annotation

  # Apply type comments to the MAKE_FUNCTION opcodes
  for start, (end, op) in sorted(
      visitor.make_function_ops.items(), reverse=True
  ):
    for i in range(start, end):
      # Take the first comment we find as the function typecomment.
      if i in annotations:
        # Record the line number of the comment for error messages.
        op.annotation = (annotations[i].annotation, i)
        break
  return code


def adjust_returns(code, block_returns):
  """Adjust line numbers for return statements in with blocks."""

  rets = {k: iter(v) for k, v in block_returns}
  for block in code.order:
    for op in block:
      if op.__class__.__name__ in ("RETURN_VALUE", "RETURN_CONST"):
        if op.line in rets:
          lines = rets[op.line]
          new_line = next(lines, None)
          if new_line:
            op.line = new_line
