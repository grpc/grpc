"""Objects modelling VM state. (Frames etc.)."""

import collections
from collections.abc import Collection
import itertools
import logging
from typing import Any, Union

from pytype import compare
from pytype import metrics
from pytype import module_utils
from pytype import utils
from pytype.abstract import abstract
from pytype.blocks import blocks
from pytype.typegraph import cfg

log = logging.getLogger(__name__)

# A special constant, returned by split_conditions() to signal that the
# condition cannot be satisfied with any known bindings.
UNSATISFIABLE = object()

# Represents the `not None` condition for restrict_condition().
NOT_NONE = object()

FrameType = Union["SimpleFrame", "Frame"]
# This should be context.Context, which can't be imported due to a circular dep.
_ContextType = Any


class FrameState(utils.ContextWeakrefMixin):
  """Immutable state object, for attaching to opcodes."""

  __slots__ = ["block_stack", "data_stack", "node", "exception", "why"]

  def __init__(self, data_stack, block_stack, node, ctx, exception, why):
    super().__init__(ctx)
    self.data_stack = data_stack
    self.block_stack = block_stack
    self.node = node
    self.exception = exception
    self.why = why

  @classmethod
  def init(cls, node, ctx):
    return FrameState((), (), node, ctx, False, None)

  def __setattribute__(self):
    raise AttributeError("States are immutable.")

  def set_why(self, why):
    return FrameState(
        self.data_stack,
        self.block_stack,
        self.node,
        self.ctx,
        self.exception,
        why,
    )

  def set_stack(self, new_stack):
    return FrameState(
        new_stack,
        self.block_stack,
        self.node,
        self.ctx,
        self.exception,
        self.why,
    )

  def push(self, *values):
    """Push value(s) onto the value stack."""
    return self.set_stack(self.data_stack + tuple(values))

  def peek(self, n):
    """Get a value `n` entries down in the stack, without changing the stack."""
    return self.data_stack[-n]

  def top(self):
    return self.data_stack[-1]

  def topn(self, n):
    if n > 0:
      return self.data_stack[-n:]
    else:
      return ()

  def pop(self):
    """Pop a value from the value stack."""
    if not self.data_stack:
      raise IndexError("Trying to pop from an empty stack")
    value = self.data_stack[-1]
    return self.set_stack(self.data_stack[:-1]), value

  def pop_and_discard(self):
    """Pop a value from the value stack and discard it."""
    return self.set_stack(self.data_stack[:-1])

  def popn(self, n):
    """Return n values, ordered oldest-to-newest."""
    if not n:
      # Not an error: E.g. function calls with no parameters pop zero items
      return self, ()
    if len(self.data_stack) < n:
      raise IndexError(
          "Trying to pop %d values from stack of size %d"
          % (n, len(self.data_stack))
      )
    values = self.data_stack[-n:]
    return self.set_stack(self.data_stack[:-n]), values

  def set_top(self, value):
    """Replace top of data stack with value."""
    return self.set_stack(self.data_stack[:-1] + (value,))

  def set_second(self, value):
    """Replace second element of data stack with value."""
    return self.set_stack(self.data_stack[:-2] + (value, self.data_stack[-1]))

  def rotn(self, n):
    """Rotate the top n values by one."""
    if len(self.data_stack) < n:
      raise IndexError(
          "Trying to rotate %d values from stack of size %d"
          % (n, len(self.data_stack))
      )
    top = self.data_stack[-1]
    rot = self.data_stack[-n:-1]
    return self.set_stack(self.data_stack[:-n] + (top,) + rot)

  def swap(self, n):
    """Swap the top of the data stack with the value in position n."""
    if len(self.data_stack) < n:
      raise IndexError(
          "Trying to swap value %d in stack of size %d"
          % (n, len(self.data_stack))
      )
    top = self.data_stack[-1]
    nth = self.data_stack[-n]
    in_between = self.data_stack[(-n + 1) : -1]
    rest = self.data_stack[:-n]
    return self.set_stack(rest + (top,) + in_between + (nth,))

  def push_block(self, block):
    """Push a block on to the block stack."""
    return FrameState(
        self.data_stack,
        self.block_stack + (block,),
        self.node,
        self.ctx,
        self.exception,
        self.why,
    )

  def pop_block(self):
    """Pop a block from the block stack."""
    block = self.block_stack[-1]
    return (
        FrameState(
            self.data_stack,
            self.block_stack[:-1],
            self.node,
            self.ctx,
            self.exception,
            self.why,
        ),
        block,
    )

  def change_cfg_node(self, node: cfg.CFGNode) -> "FrameState":
    if self.node is node:
      return self
    return FrameState(
        self.data_stack,
        self.block_stack,
        node,
        self.ctx,
        self.exception,
        self.why,
    )

  def connect_to_cfg_node(self, node):
    self.node.ConnectTo(node)
    return self.change_cfg_node(node)

  def forward_cfg_node(self, new_name, condition=None):
    """Create a new CFG Node connected to the current cfg node.

    Args:
      new_name: A name for the new node.
      condition: A cfg.Binding representing the condition that needs to be true
        for this node to be reached.

    Returns:
      A new state which is the same as this state except for the node, which is
      the new one.
    """
    new_node = self.ctx.connect_new_cfg_node(self.node, new_name, condition)
    return self.change_cfg_node(new_node)

  def merge_into(self, other):
    """Merge with another state."""
    if other is None:
      return self
    assert len(self.data_stack) == len(other.data_stack), (
        self.data_stack,
        other.data_stack,
    )
    assert len(self.block_stack) == len(other.block_stack), (
        self.block_stack,
        other.block_stack,
    )
    both = list(zip(self.data_stack, other.data_stack))
    if any(v1 is not v2 for v1, v2 in both):
      for v, o in both:
        o.PasteVariable(v, None)
    if self.node is not other.node:
      self.node.ConnectTo(other.node)
      return FrameState(
          other.data_stack,
          self.block_stack,
          other.node,
          self.ctx,
          self.exception,
          self.why,
      )
    return self

  def set_exception(self):
    return FrameState(
        self.data_stack,
        self.block_stack,
        self.ctx.connect_new_cfg_node(self.node, "SetException"),
        self.ctx,
        True,
        self.why,
    )


class SimpleFrame:
  """A lightweight placeholder frame.

  A frame used when we need a placeholder on the stack, e.g., to imitate a
  function call in order to analyze a function, or to create a dummy stack for
  error logging.
  """

  def __init__(self, opcode=None, node=None, f_globals=None):
    self.f_code = None  # for recursion detection
    self.f_builtins = None
    self.f_globals = f_globals
    self.current_opcode = opcode  # for memoization of unknowns
    self.node = node
    self.substs = ()
    self.func = None
    self.skip_in_tracebacks = False


class Frame(utils.ContextWeakrefMixin):
  """An interpreter frame.

  This contains the local value and block stacks and the associated code and
  pointer. The most complex usage is with generators in which a frame is stored
  and then repeatedly reactivated. Other than that frames are created executed
  and then discarded.

  Attributes:
    f_code: The code object this frame is executing.
    f_globals: The globals dict used for global name resolution.
    f_locals: The locals used for name resolution. Will be modified by
      Frame.__init__ if callargs is passed.
    f_builtins: Similar for builtins.
    f_back: The frame above self on the stack.
    f_lineno: The first line number of the code object.
    ctx: The abstract context we belong to.
    node: The node at which the frame is created.
    states: A mapping from opcodes to FrameState objects.
    cells: local variables bound in a closure, or used in a closure.
    block_stack: A stack of blocks used to manage exceptions, loops, and
      "with"s.
    data_stack: The value stack that is used for instruction operands.
    allowed_returns: The return annotation of this function.
    check_return: Whether the actual return type of a call should be checked
      against allowed_returns.
    return_variable: The return value of this function, as a Variable.
    yield_variable: The yield value of this function, as a Variable.
  """

  def __init__(
      self,
      node: cfg.CFGNode,
      ctx: _ContextType,
      f_code: blocks.OrderedCode,
      f_globals: abstract.LazyConcreteDict,
      f_locals: abstract.LazyConcreteDict,
      f_back: FrameType,
      callargs: dict[str, cfg.Variable],
      closure: tuple[cfg.Variable, ...] | None,
      func: cfg.Binding | None,
      first_arg: cfg.Variable | None,
      substs: Collection[dict[str, cfg.Variable]],
  ):
    """Initialize a special frame as needed by TypegraphVirtualMachine.

    Args:
      node: The current CFG graph node.
      ctx: The owning abstract context.
      f_code: The code object to execute in this frame.
      f_globals: The global context to execute in as a SimpleValue as used by
        TypegraphVirtualMachine.
      f_locals: Local variables. Will be modified if callargs is passed.
      f_back: The frame above this one on the stack.
      callargs: Additional function arguments to store in f_locals.
      closure: A tuple containing the new co_freevars.
      func: Optionally, a binding to the function this frame corresponds to.
      first_arg: First argument to the function.
      substs: Maps from type parameter names in scope for this frame to their
        possible values.

    Raises:
      NameError: If we can't resolve any references into the outer frame.
    """
    super().__init__(ctx)
    self.node = node
    self.current_opcode = None
    self.f_code = f_code
    self.states = {}
    self.f_globals = f_globals
    self.f_locals = f_locals
    self.f_back = f_back
    if f_back and f_back.f_builtins:
      self.f_builtins = f_back.f_builtins
    else:
      _, bltin = self.ctx.attribute_handler.get_attribute(
          self.ctx.root_node, f_globals, "__builtins__"
      )
      (builtins_pu,) = bltin.bindings
      self.f_builtins = builtins_pu.data
    self.f_lineno = f_code.firstlineno
    # The first argument is used to make Python 3 super calls when super is not
    # passed any arguments.
    self.first_arg = first_arg

    self.allowed_returns = None
    self.check_return = False
    self.return_variable = self.ctx.program.NewVariable()
    self.yield_variable = self.ctx.program.NewVariable()

    # Keep track of the current opcode block and and block targets we add while
    # executing it; they can potentially be removed if the block returns early.
    self.current_block = None
    self.targets = collections.defaultdict(list)

    # A map from function name to @typing.overload-decorated signatures. The
    # overloads are copied to the implementation in InterpreterFunction.make.
    self.overloads = collections.defaultdict(list)

    # A closure g communicates with its outer function f through two
    # fields in CodeType (both of which are tuples of strings):
    # f.co_cellvars: All f-local variables that are used in g (or any other
    #                closure).
    # g.co_freevars: All variables from f that g uses.
    # Also, note that f.co_cellvars will only also be in f.co_varnames
    # if they are also parameters of f (because co_varnames[0:co_argcount] are
    # always the parameters), but won't otherwise.
    # Cells 0 .. num(cellvars)-1 : cellvar; num(cellvars) .. end : freevar
    self.closure = closure
    freevars = closure or []
    assert len(f_code.freevars) == len(freevars)
    if self.ctx.python_version < (3, 11):
      cell_names = f_code.cellvars
    elif freevars:
      cell_names = f_code.localsplus[: -len(freevars)]
    else:
      cell_names = f_code.localsplus
    self.cells = [self.ctx.program.NewVariable() for _ in cell_names]
    self.cells.extend(freevars)
    if callargs:
      for name, value in sorted(callargs.items()):
        if name in f_code.cellvars:
          i = cell_names.index(name)
          self.cells[i].PasteVariable(value, node)
        else:
          self.ctx.attribute_handler.set_attribute(node, f_locals, name, value)
    # Python 3 supports calling 'super' without any arguments. In such a case
    # the implicit type argument is inserted into __build_class__'s cellvars
    # and propagated as a closure variable to all method/functions calling
    # 'super' without any arguments.
    # If this is a frame for the function called by __build_class__ (see
    # abstract.BuildClass), then we will store a reference to the variable
    # corresponding to the cellvar named "__class__" separately for convenient
    # access. After the class is built, abstract.BuildClass.call will add the
    # binding for the new class into this variable.
    self.class_closure_var = None
    if func and isinstance(func.data, abstract.InterpreterFunction):
      closure_name = abstract.BuildClass.CLOSURE_NAME
      if func.data.is_class_builder and closure_name in f_code.cellvars:
        self.class_closure_var = self.get_cell_by_name(closure_name)
    self.func = func
    self.substs = substs
    # Do not add to error tracebacks
    self.skip_in_tracebacks = False

    # Set the module name (used in logging)
    if f_code.filename:
      self.module_name = module_utils.path_to_module_name(f_code.filename)
    else:
      self.module_name = ""

    # All InterpreterFunction objects created while this frame was at the top of
    # the frame stack.
    self.functions_created_in_frame: dict[
        str, list[abstract.InterpreterFunction]
    ] = collections.defaultdict(list)

  def __repr__(self):  # pragma: no cover
    return "<Frame at 0x%08x: %r @ %d>" % (
        id(self),
        self.f_code.filename,
        self.f_lineno,
    )

  def copy_free_vars(self, n):
    offset = len(self.cells) - len(self.f_code.freevars)
    for i in range(n):
      self.cells[i + offset] = self.closure[i]

  @property
  def type_params(self):
    return set(itertools.chain.from_iterable(self.substs))

  def lookup_name(self, target_name):
    for store in (self.f_locals, self.f_globals, self.f_builtins):
      if store is not None and target_name in store.members:
        return store.members[target_name]
    return self.get_cell_by_name(target_name)

  def get_cell_by_name(self, name):
    return self.cells[self.f_code.get_cell_index(name)]


class Condition:
  """Represents a condition due to if-splitting.

  Properties:
    node: A CFGNode.
    binding: A Binding for the condition's constraints.
  """

  def __init__(self, node, dnf):
    # The condition is represented by a dummy variable with a single binding
    # to None.  The origins for this binding are the dnf clauses.
    self._var = node.program.NewVariable()
    self._binding = self._var.AddBinding(None)
    for clause in dnf:
      sources = set(clause)
      self._binding.AddOrigin(node, sources)

  @property
  def binding(self):
    return self._binding


_restrict_counter = metrics.MapCounter("state_restrict")


def _match_condition(value, condition):
  if isinstance(condition, bool):
    return compare.compatible_with(value, condition)
  elif condition is None:
    return compare.compatible_with_none(value)
  else:
    assert condition is NOT_NONE
    return value.full_name != "builtins.NoneType"


def restrict_condition(node, var, condition):
  """Return a restricted condition based on filtered bindings.

  Args:
    node: The CFGNode.
    var: A variable.
    condition: A value that we will check each binding for compatibility with.

  Returns:
    A Condition or None. Each binding of the variable is checked for
    compatibility with the condition. If either no bindings match, or all
    bindings match, then None is returned.  Otherwise a new Condition is built
    from the specified, compatible, bindings.
  """
  dnf = []
  restricted = False
  for b in var.bindings:
    match_result = _match_condition(b.data, condition)
    if match_result:
      dnf.append([b])  # the binding may match the condition
    else:
      restricted = True  # the binding cannot match the condition
  if not dnf:
    _restrict_counter.inc("unsatisfiable")
    return UNSATISFIABLE
  elif restricted:
    _restrict_counter.inc("restricted")
    return Condition(node, dnf)
  else:
    _restrict_counter.inc("unrestricted")
    return None


def _is_or_is_not_cmp(left, right, is_not=False):
  """Implementation of 'left is right' amd 'left is not right'."""
  if isinstance(left, abstract.PythonConstant) and isinstance(
      right, abstract.PythonConstant
  ):
    if left.cls != right.cls:
      return is_not
    return is_not ^ (left.pyval == right.pyval)
  elif isinstance(left, abstract.Instance) and isinstance(
      right, abstract.Instance
  ):
    if left.cls != right.cls:
      # If those were the same they could be the same but we can't be sure from
      # comparing types.
      return is_not
    return None
  elif isinstance(left, abstract.Class) and isinstance(right, abstract.Class):
    # types are singletons. We use the name so that, e.g., two different
    # TupleClass instances compare as identical.
    return is_not ^ (left.full_name == right.full_name)
  else:
    return None


def is_cmp(left, right):
  return _is_or_is_not_cmp(left, right, is_not=False)


def is_not_cmp(left, right):
  return _is_or_is_not_cmp(left, right, is_not=True)
