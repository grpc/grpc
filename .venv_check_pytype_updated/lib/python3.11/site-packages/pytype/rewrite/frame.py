"""A frame of an abstract VM for type analysis of python bytecode."""

from collections.abc import Mapping, Sequence
import logging
from typing import Any, Optional

from pycnite import marshal as pyc_marshal
from pytype import datatypes
from pytype.blocks import blocks
from pytype.rewrite import context
from pytype.rewrite import function_call_helper
from pytype.rewrite import operators
from pytype.rewrite import stack
from pytype.rewrite.abstract import abstract
from pytype.rewrite.flow import conditions
from pytype.rewrite.flow import frame_base
from pytype.rewrite.flow import variables

log = logging.getLogger(__name__)

# Type aliases
_Var = variables.Variable[abstract.BaseValue]
_VarMap = Mapping[str, _Var]
_FrameFunction = abstract.InterpreterFunction['Frame']

# This enum will be used frequently, so alias it
_Flags = pyc_marshal.Flags


class _ShadowedNonlocals:
  """Tracks shadowed nonlocal names."""

  def __init__(self):
    self._enclosing: set[str] = set()
    self._globals: set[str] = set()

  def add_enclosing(self, name: str) -> None:
    self._enclosing.add(name)

  def add_global(self, name: str) -> None:
    self._globals.add(name)

  def has_enclosing(self, name: str):
    return name in self._enclosing

  def has_global(self, name: str):
    return name in self._globals

  def get_global_names(self) -> frozenset[str]:
    return frozenset(self._globals)

  def get_enclosing_names(self) -> frozenset[str]:
    return frozenset(self._enclosing)


class Frame(frame_base.FrameBase[abstract.BaseValue]):
  """Virtual machine frame.

  Attributes:
    name: The name of the frame.
    final_locals: The final `locals` dictionary after the frame finishes
      executing, with Variables flattened to BaseValues.
  """

  def __init__(
      self,
      ctx: context.Context,
      name: str,
      code: blocks.OrderedCode,
      *,
      initial_locals: _VarMap = datatypes.EMPTY_MAP,
      initial_enclosing: _VarMap = datatypes.EMPTY_MAP,
      initial_globals: _VarMap = datatypes.EMPTY_MAP,
      f_back: Optional['Frame'] = None,
  ):
    super().__init__(code, initial_locals)
    self._ctx = ctx
    self.name = name  # name of the frame
    # Final values of locals, unwrapped from variables
    self.final_locals: Mapping[str, abstract.BaseValue] = None

    # Sanity checks: a module frame should have the same locals and globals. A
    # frame should have an enclosing scope only if it has a parent (f_back).
    assert not self._is_module_frame or initial_locals == initial_globals
    assert f_back or not initial_enclosing

    # Initial variables in enclosing and global scopes
    self._initial_enclosing = initial_enclosing
    self._initial_globals = initial_globals
    self._f_back = f_back  # the frame that created this one, if any
    self._stack = stack.DataStack()  # data stack
    # Names of nonlocals shadowed in the current frame
    self._shadowed_nonlocals = _ShadowedNonlocals()
    # All functions and classes created during execution
    self._functions: list[_FrameFunction] = []
    self._classes: list[abstract.InterpreterClass] = []
    # All variables returned via RETURN_VALUE/RETURN_CONST
    self._returns: list[_Var] = []
    # Handler for function calls.
    self._call_helper = function_call_helper.FunctionCallHelper(ctx, self)

  def __repr__(self):
    return f'Frame({self.name})'

  @classmethod
  def make_module_frame(
      cls,
      ctx: context.Context,
      code: blocks.OrderedCode,
      initial_globals: _VarMap,
  ) -> 'Frame':
    return cls(
        ctx=ctx,
        name='__main__',
        code=code,
        initial_locals=initial_globals,
        initial_enclosing={},
        initial_globals=initial_globals,
        f_back=None,
    )

  @property
  def functions(self) -> Sequence[_FrameFunction]:
    return tuple(self._functions)

  @property
  def classes(self) -> Sequence[abstract.InterpreterClass]:
    return tuple(self._classes)

  @property
  def _is_module_frame(self) -> bool:
    return self.name == '__main__'

  @property
  def stack(self) -> Sequence['Frame']:
    return (self._f_back.stack if self._f_back else []) + [self]

  def run(self) -> None:
    log.info('Running frame: %s', self.name)
    assert not self._stack
    while True:
      try:
        self.step()
        self._log_stack()
      except frame_base.FrameConsumedError:
        break
    assert not self._stack
    log.info('Finished running frame: %s', self.name)
    if self._f_back and self._f_back.final_locals is None:
      live_parent = self._f_back
      log.info('Resuming frame: %s', live_parent.name)
    else:
      live_parent = None
    self._merge_nonlocals_into(live_parent)
    # Set the current state to None so that the load_* and store_* methods
    # cannot be used to modify finalized locals.
    self._current_state = None
    self.final_locals = datatypes.immutabledict({
        name: abstract.join_values(self._ctx, var.values)
        for name, var in self._final_locals.items()
    })

  def _log_stack(self):
    log.debug('stack: %r', self._stack)

  def store_local(self, name: str, var: _Var) -> None:
    self._current_state.store_local(name, var)

  def store_enclosing(self, name: str, var: _Var) -> None:
    # We shadow the name from the enclosing scope. We will merge it into f_back
    # when the current frame finishes.
    self._current_state.store_local(name, var)
    self._shadowed_nonlocals.add_enclosing(name)

  def store_global(self, name: str, var: _Var) -> None:
    # We allow modifying globals only when executing the module frame.
    # Otherwise, we shadow the global in current frame. Either way, the behavior
    # is equivalent to storing the global as a local.
    self._current_state.store_local(name, var)
    self._shadowed_nonlocals.add_global(name)

  def store_deref(self, name: str, var: _Var) -> None:
    # When a name from a parent frame is referenced in a child frame, we make a
    # conceptual distinction between the parent's local scope and the child's
    # enclosing scope. However, at runtime, writing to both is the same
    # operation (STORE_DEREF), so it's convenient to have a store method that
    # emulates this.
    if name in self._initial_enclosing:
      self.store_enclosing(name, var)
    else:
      self.store_local(name, var)

  def _shadows_enclosing(self, name: str) -> bool:
    """Does name shadow a variable from the enclosing scope?"""
    return self._shadowed_nonlocals.has_enclosing(name)

  def _shadows_global(self, name: str) -> bool:
    """Does name shadow a variable from the global scope?"""
    if self._is_module_frame:
      # This is the global scope, and so `name` cannot shadow anything.
      return False
    return self._shadowed_nonlocals.has_global(name)

  def load_local(self, name) -> _Var:
    if self._shadows_enclosing(name) or self._shadows_global(name):
      raise KeyError(name)
    return self._current_state.load_local(name)

  def load_enclosing(self, name) -> _Var:
    if self._shadows_enclosing(name):
      return self._current_state.load_local(name)
    return self._initial_enclosing[name].with_name(name)

  def load_global(self, name) -> _Var:
    if self._shadows_global(name):
      return self._current_state.load_local(name)
    try:
      if self._is_module_frame:
        return self._current_state.load_local(name)
      else:
        return self._initial_globals[name].with_name(name)
    except KeyError:
      return self.load_builtin(name)

  def load_builtin(self, name) -> _Var:
    builtin = self._ctx.abstract_loader.load_builtin(name)
    return builtin.to_variable(name)

  def load_deref(self, name) -> _Var:
    # When a name from a parent frame is referenced in a child frame, we make a
    # conceptual distinction between the parent's local scope and the child's
    # enclosing scope. However, at runtime, reading from both is the same
    # operation (LOAD_DEREF), so it's convenient to have a load method that
    # emulates this.
    try:
      return self.load_local(name)
    except KeyError:
      return self.load_enclosing(name)

  def make_child_frame(
      self,
      func: _FrameFunction,
      initial_locals: Mapping[str, _Var],
  ) -> 'Frame':
    if self._final_locals:
      current_locals = {
          name: val.to_variable() for name, val in self.final_locals.items()
      }
    else:
      current_locals = self._current_state.get_locals()
    initial_enclosing = {}
    for name in func.enclosing_scope:
      if name in current_locals:
        assert not self._shadows_global(name)
        initial_enclosing[name] = current_locals[name]
      else:
        initial_enclosing[name] = self._initial_enclosing[name]
    if self._is_module_frame:
      # The module frame's locals are the most up-to-date globals.
      initial_globals = current_locals
    else:
      initial_globals = dict(self._initial_globals)
      for name in self._shadowed_nonlocals.get_global_names():
        initial_globals[name] = current_locals[name]
    return Frame(
        ctx=self._ctx,
        name=func.name,
        code=func.code,
        initial_locals=initial_locals,
        initial_enclosing=initial_enclosing,
        initial_globals=initial_globals,
        f_back=self,
    )

  def get_return_value(self) -> abstract.BaseValue:
    values = sum((ret.values for ret in self._returns), ())
    return abstract.join_values(self._ctx, values)

  def _merge_nonlocals_into(self, frame: Optional['Frame']) -> None:
    # Perform any STORE_ATTR operations recorded in locals.
    for name, var in self._final_locals.items():
      target_name, dot, attr_name = name.rpartition('.')
      if not dot or target_name not in self._final_locals:
        continue
      # If the target is present on 'frame', then we merge the attribute values
      # into the frame so that any conditions on the bindings are preserved.
      # Otherwise, we store the attribute on the target.
      target_var = self._final_locals[target_name]
      if frame:
        try:
          frame_target_var = frame.load_local(target_name)
        except KeyError:
          store_on_target = True
        else:
          store_on_target = target_var.values != frame_target_var.values
      else:
        store_on_target = True
      if store_on_target:
        value = abstract.join_values(self._ctx, var.values)
        for target in target_var.values:
          log.info(
              'Storing attribute on %r: %s -> %r', target, attr_name, value
          )
          target.set_attribute(attr_name, value)
      else:
        frame.store_local(name, var)
    if not frame:
      return
    # Store nonlocals.
    for name in self._shadowed_nonlocals.get_enclosing_names():
      var = self._final_locals[name]
      frame.store_deref(name, var)
    for name in self._shadowed_nonlocals.get_global_names():
      var = self._final_locals[name]
      frame.store_global(name, var)

  def _call_function(
      self,
      func_var: _Var,
      args: abstract.Args['Frame'],
  ) -> None:
    ret_values = []
    for func in func_var.values:
      if isinstance(func, (abstract.BaseFunction, abstract.InterpreterClass)):
        ret = func.call(args)
        ret_values.append(ret.get_return_value())
      elif func is self._ctx.consts.singles['__build_class__']:
        cls = self._call_helper.build_class(args)
        log.info('Created class: %r', cls)
        self._classes.append(cls)
        ret_values.append(cls)
      else:
        raise NotImplementedError('CALL not fully implemented')
    self._stack.push(
        variables.Variable(tuple(variables.Binding(v) for v in ret_values))
    )

  def load_attr(self, target_var: _Var, attr_name: str) -> _Var:
    if target_var.name:
      name = f'{target_var.name}.{attr_name}'
    else:
      name = None
    try:
      # Check if we've stored the attribute in the current frame.
      return self.load_local(name)
    except KeyError as e:
      # We're loading an attribute without a locally stored value.
      attr_bindings = []
      for target in target_var.values:
        attr = target.get_attribute(attr_name)
        if not attr:
          raise AttributeError(attr_name) from e
        # TODO(b/241479600): If there's a condition on the target binding, we
        # should copy it.
        attr_bindings.append(variables.Binding(attr))
      return variables.Variable(tuple(attr_bindings), name)

  def _load_method(
      self, instance_var: _Var, method_name: str
  ) -> tuple[_Var, _Var]:
    # https://docs.python.org/3/library/dis.html#opcode-LOAD_METHOD says that
    # this opcode should push two values onto the stack: either the unbound
    # method and its `self` or NULL and the bound method. Since we always
    # retrieve a bound method, we push the NULL
    return (
        self._ctx.consts.singles['NULL'].to_variable(),
        self.load_attr(instance_var, method_name),
    )

  def _pop_jump_if_false(self, opcode):
    unused_var = self._stack.pop()
    # TODO(b/324465215): Construct the real conditions for this jump.
    jump_state = self._current_state.with_condition(conditions.Condition())
    self._merge_state_into(jump_state, opcode.argval)
    nojump_state = self._current_state.with_condition(conditions.Condition())
    self._merge_state_into(nojump_state, opcode.next.index)

  def _replace_atomic_stack_value(
      self, n, new_value: abstract.BaseValue
  ) -> None:
    cur_var = self._stack.peek(n)
    self._stack.replace(n, cur_var.with_value(new_value))

  # ---------------------------------------------------------------
  # Opcodes with no typing effects

  def byte_NOP(self, opcode):
    del opcode  # unused

  def byte_PRINT_EXPR(self, opcode):
    del opcode  # unused
    self._stack.pop_and_discard()

  def byte_PRECALL(self, opcode):
    # Internal cpython use
    del opcode  # unused

  def byte_RESUME(self, opcode):
    # Internal cpython use
    del opcode  # unused

  # ---------------------------------------------------------------
  # Load and store operations

  def _get_const(self, oparg):
    const = self._code.consts[oparg]
    if isinstance(const, tuple):
      # Tuple literals with all primitive elements are stored as a single raw
      # constant; we need to wrap each element in a variable for consistency
      # with tuples created via BUILD_TUPLE
      val = self._ctx.abstract_loader.build_tuple(const)
    else:
      val = self._ctx.consts[const]
    return val.to_variable()

  def byte_LOAD_CONST(self, opcode):
    self._stack.push(self._get_const(opcode.arg))

  def byte_RETURN_VALUE(self, opcode):
    self._returns.append(self._stack.pop())

  def byte_RETURN_CONST(self, opcode):
    self._returns.append(self._get_const(opcode.arg))

  def byte_STORE_NAME(self, opcode):
    self.store_local(opcode.argval, self._stack.pop())

  def byte_STORE_FAST(self, opcode):
    self.store_local(opcode.argval, self._stack.pop())

  def byte_STORE_GLOBAL(self, opcode):
    self.store_global(opcode.argval, self._stack.pop())

  def byte_STORE_DEREF(self, opcode):
    self.store_deref(opcode.argval, self._stack.pop())

  def byte_STORE_ATTR(self, opcode):
    attr_name = opcode.argval
    attr, target = self._stack.popn(2)
    if not target.name:
      raise NotImplementedError('Missing target name')
    full_name = f'{target.name}.{attr_name}'
    self.store_local(full_name, attr)

  def _unpack_function_annotations(self, packed_annot):
    if self._code.python_version >= (3, 10):
      # In Python 3.10+, packed_annot is a tuple of variables:
      # (param_name1, param_type1, param_name2, param_type2, ...)
      annot_seq = abstract.get_atomic_constant(packed_annot, tuple)
      double_num_annots = len(annot_seq)
      assert not double_num_annots % 2
      annot = {}
      for i in range(double_num_annots // 2):
        name = abstract.get_atomic_constant(annot_seq[i * 2], str)
        annot[name] = annot_seq[i * 2 + 1]
    else:
      # Pre-3.10, packed_annot was a name->param_type dictionary.
      annot = abstract.get_atomic_constant(packed_annot, dict)
    return annot

  def byte_MAKE_FUNCTION(self, opcode):
    # Aliases for readability
    pop_const = lambda t: abstract.get_atomic_constant(self._stack.pop(), t)
    arg = opcode.arg
    # Get name and code object
    if self._code.python_version >= (3, 11):
      code = pop_const(blocks.OrderedCode)
      name = code.qualname
    else:
      name = pop_const(str)
      code = pop_const(blocks.OrderedCode)
    # Free vars
    if arg & _Flags.MAKE_FUNCTION_HAS_FREE_VARS:
      freevars = pop_const(tuple)
      enclosing_scope = tuple(freevar.name for freevar in freevars)
      assert all(enclosing_scope)
    else:
      enclosing_scope = ()
    # Annotations
    annot = {}
    if arg & _Flags.MAKE_FUNCTION_HAS_ANNOTATIONS:
      packed_annot = self._stack.pop()
      annot = self._unpack_function_annotations(packed_annot)
    # Defaults
    pos_defaults, kw_defaults = (), {}
    if arg & _Flags.MAKE_FUNCTION_HAS_POS_DEFAULTS:
      pos_defaults = pop_const(tuple)
    if arg & _Flags.MAKE_FUNCTION_HAS_KW_DEFAULTS:
      packed_kw_def = self._stack.pop()
      kw_defaults = packed_kw_def.get_atomic_value(abstract.Dict)
    # Make function
    del annot, pos_defaults, kw_defaults  # TODO(b/241479600): Use these
    func = abstract.InterpreterFunction(
        self._ctx, name, code, enclosing_scope, self
    )
    log.info('Created function: %s', func.name)
    if not (
        self._stack
        and self._stack.top().has_atomic_value(
            self._ctx.consts.singles['__build_class__']
        )
    ):
      # Class building makes and immediately calls a function that creates the
      # class body; we don't need to store this function for later analysis.
      self._functions.append(func)
    self._stack.push(func.to_variable())

  def byte_PUSH_NULL(self, opcode):
    del opcode  # unused
    self._stack.push(self._ctx.consts.singles['NULL'].to_variable())

  def byte_LOAD_NAME(self, opcode):
    name = opcode.argval
    try:
      var = self.load_local(name)
    except KeyError:
      var = self.load_global(name)
    self._stack.push(var)

  def byte_LOAD_FAST(self, opcode):
    name = opcode.argval
    self._stack.push(self.load_local(name))

  def byte_LOAD_DEREF(self, opcode):
    name = opcode.argval
    self._stack.push(self.load_deref(name))

  def byte_LOAD_CLOSURE(self, opcode):
    name = opcode.argval
    self._stack.push(self.load_deref(name))

  def byte_LOAD_GLOBAL(self, opcode):
    if self._code.python_version >= (3, 11) and opcode.arg & 1:
      # Compiler-generated marker that will be consumed in byte_CALL
      # We are loading a global and calling it as a function.
      self._stack.push(self._ctx.consts.singles['NULL'].to_variable())
    name = opcode.argval
    self._stack.push(self.load_global(name))

  def byte_LOAD_ATTR(self, opcode):
    attr_name = opcode.argval
    target_var = self._stack.pop()
    if self._code.python_version >= (3, 12) and opcode.arg & 1:
      (var1, var2) = self._load_method(target_var, attr_name)
      self._stack.push(var1)
      self._stack.push(var2)
    else:
      self._stack.push(self.load_attr(target_var, attr_name))

  def byte_LOAD_METHOD(self, opcode):
    method_name = opcode.argval
    instance_var = self._stack.pop()
    (var1, var2) = self._load_method(instance_var, method_name)
    self._stack.push(var1)
    self._stack.push(var2)

  def byte_IMPORT_NAME(self, opcode):
    full_name = opcode.argval
    unused_level_var, fromlist = self._stack.popn(2)
    # The IMPORT_NAME for an "import a.b.c" will push the module "a".
    # However, for "from a.b.c import Foo" it'll push the module "a.b.c". Those
    # two cases are distinguished by whether fromlist is None or not.
    if fromlist.has_atomic_value(self._ctx.consts[None]):
      module_name = full_name.split('.', 1)[0]  # "a.b.c" -> "a"
    else:
      module_name = full_name
    if self._ctx.pytd_loader.import_name(module_name):
      module = abstract.Module(self._ctx, module_name)
    else:
      self._ctx.errorlog.import_error(self.stack, module_name)
      module = self._ctx.consts.Any
    if full_name != module_name and not self._ctx.pytd_loader.import_name(
        full_name
    ):
      # Even if we're only importing "a", make sure "a.b.c" is valid.
      self._ctx.errorlog.import_error(self.stack, full_name)
    self._stack.push(module.to_variable())

  def byte_IMPORT_FROM(self, opcode):
    attr_name = opcode.argval
    module = self._stack.top().get_atomic_value()
    attr = module.get_attribute(attr_name)
    if not attr:
      module_binding = module.to_variable().bindings[0]
      self._ctx.errorlog.attribute_error(self.stack, module_binding, attr_name)
      attr = self._ctx.consts.Any
    self._stack.push(attr.to_variable())

  # ---------------------------------------------------------------
  # Function and method calls

  def byte_KW_NAMES(self, opcode):
    # Stores a list of kw names to be retrieved by CALL
    self._call_helper.set_kw_names(opcode.argval)

  def byte_CALL(self, opcode):
    sentinel, *rest = self._stack.popn(opcode.arg + 2)
    if not sentinel.has_atomic_value(self._ctx.consts.singles['NULL']):
      raise NotImplementedError('CALL not fully implemented')
    func, *args = rest
    callargs = self._call_helper.make_function_args(args)
    self._call_function(func, callargs)

  def byte_CALL_FUNCTION(self, opcode):
    args = self._stack.popn(opcode.arg)
    func = self._stack.pop()
    callargs = self._call_helper.make_function_args(args)
    self._call_function(func, callargs)

  def byte_CALL_FUNCTION_KW(self, opcode):
    kwnames_var = self._stack.pop()
    args = self._stack.popn(opcode.arg)
    func = self._stack.pop()
    kwnames = [
        abstract.get_atomic_constant(key, str)
        for key in abstract.get_atomic_constant(kwnames_var, tuple)
    ]
    self._call_helper.set_kw_names(kwnames)
    callargs = self._call_helper.make_function_args(args)
    self._call_function(func, callargs)

  def byte_CALL_FUNCTION_EX(self, opcode):
    if opcode.arg & _Flags.CALL_FUNCTION_EX_HAS_KWARGS:
      starstarargs = self._stack.pop()
    else:
      starstarargs = None
    starargs = self._stack.pop()
    callargs = self._call_helper.make_function_args_ex(starargs, starstarargs)
    # Retrieve and call the function
    func = self._stack.pop()
    if self._code.python_version >= (3, 11):
      # the compiler puts a NULL on the stack before function calls
      self._stack.pop_and_discard()
    self._call_function(func, callargs)

  def byte_CALL_METHOD(self, opcode):
    args = self._stack.popn(opcode.arg)
    func = self._stack.pop()
    # pop the NULL off the stack (see LOAD_METHOD)
    self._stack.pop_and_discard()
    callargs = abstract.Args(posargs=tuple(args), frame=self)
    self._call_function(func, callargs)

  # Pytype tracks variables in enclosing scopes by name rather than emulating
  # the runtime's approach with cells and freevars, so we can ignore the opcodes
  # that deal with the latter.
  def byte_MAKE_CELL(self, opcode):
    del opcode  # unused

  def byte_COPY_FREE_VARS(self, opcode):
    del opcode  # unused

  def byte_LOAD_BUILD_CLASS(self, opcode):
    self._stack.push(self._ctx.consts.singles['__build_class__'].to_variable())

  # ---------------------------------------------------------------
  # Operators

  def unary_operator(self, name):
    x = self._stack.pop()
    f = self.load_attr(x, name)
    self._call_function(f, abstract.Args())

  def binary_operator(self, name):
    (x, y) = self._stack.popn(2)
    ret = operators.call_binary(self._ctx, name, x, y)
    self._stack.push(ret)

  def inplace_operator(self, name):
    (x, y) = self._stack.popn(2)
    ret = operators.call_inplace(self._ctx, self, name, x, y)
    self._stack.push(ret)

  def byte_UNARY_NEGATIVE(self, opcode):
    self.unary_operator('__neg__')

  def byte_UNARY_POSITIVE(self, opcode):
    self.unary_operator('__pos__')

  def byte_UNARY_INVERT(self, opcode):
    self.unary_operator('__invert__')

  def byte_BINARY_MATRIX_MULTIPLY(self, opcode):
    self.binary_operator('__matmul__')

  def byte_BINARY_ADD(self, opcode):
    self.binary_operator('__add__')

  def byte_BINARY_SUBTRACT(self, opcode):
    self.binary_operator('__sub__')

  def byte_BINARY_MULTIPLY(self, opcode):
    self.binary_operator('__mul__')

  def byte_BINARY_MODULO(self, opcode):
    self.binary_operator('__mod__')

  def byte_BINARY_LSHIFT(self, opcode):
    self.binary_operator('__lshift__')

  def byte_BINARY_RSHIFT(self, opcode):
    self.binary_operator('__rshift__')

  def byte_BINARY_AND(self, opcode):
    self.binary_operator('__and__')

  def byte_BINARY_XOR(self, opcode):
    self.binary_operator('__xor__')

  def byte_BINARY_OR(self, opcode):
    self.binary_operator('__or__')

  def byte_BINARY_FLOOR_DIVIDE(self, opcode):
    self.binary_operator('__floordiv__')

  def byte_BINARY_TRUE_DIVIDE(self, opcode):
    self.binary_operator('__truediv__')

  def byte_BINARY_POWER(self, opcode):
    self.binary_operator('__pow__')

  def byte_BINARY_SUBSCR(self, opcode):
    obj_var, subscr_var = self._stack.popn(2)
    try:
      # See if we are specialising a generic class
      obj = obj_var.get_atomic_value(abstract.SimpleClass)
    except ValueError:
      # If not, proceed with the regular binary operator call
      return self.binary_operator('__getitem__')
    ret = obj.set_type_parameters(subscr_var)
    self._stack.push(ret.to_variable())

  def byte_INPLACE_MATRIX_MULTIPLY(self, opcode):
    self.inplace_operator('__imatmul__')

  def byte_INPLACE_ADD(self, opcode):
    self.inplace_operator('__iadd__')

  def byte_INPLACE_SUBTRACT(self, opcode):
    self.inplace_operator('__isub__')

  def byte_INPLACE_MULTIPLY(self, opcode):
    self.inplace_operator('__imul__')

  def byte_INPLACE_MODULO(self, opcode):
    self.inplace_operator('__imod__')

  def byte_INPLACE_POWER(self, opcode):
    self.inplace_operator('__ipow__')

  def byte_INPLACE_LSHIFT(self, opcode):
    self.inplace_operator('__ilshift__')

  def byte_INPLACE_RSHIFT(self, opcode):
    self.inplace_operator('__irshift__')

  def byte_INPLACE_AND(self, opcode):
    self.inplace_operator('__iand__')

  def byte_INPLACE_XOR(self, opcode):
    self.inplace_operator('__ixor__')

  def byte_INPLACE_OR(self, opcode):
    self.inplace_operator('__ior__')

  def byte_INPLACE_FLOOR_DIVIDE(self, opcode):
    self.inplace_operator('__ifloordiv__')

  def byte_INPLACE_TRUE_DIVIDE(self, opcode):
    self.inplace_operator('__itruediv__')

  def byte_BINARY_OP(self, opcode):
    """Implementation of BINARY_OP opcode."""
    # Python 3.11 unified a lot of BINARY_* and INPLACE_* opcodes into a single
    # BINARY_OP. The underlying operations remain unchanged, so we can just
    # dispatch to them.
    binops = [
        self.byte_BINARY_ADD,
        self.byte_BINARY_AND,
        self.byte_BINARY_FLOOR_DIVIDE,
        self.byte_BINARY_LSHIFT,
        self.byte_BINARY_MATRIX_MULTIPLY,
        self.byte_BINARY_MULTIPLY,
        self.byte_BINARY_MODULO,  # NB_REMAINDER in 3.11
        self.byte_BINARY_OR,
        self.byte_BINARY_POWER,
        self.byte_BINARY_RSHIFT,
        self.byte_BINARY_SUBTRACT,
        self.byte_BINARY_TRUE_DIVIDE,
        self.byte_BINARY_XOR,
        self.byte_INPLACE_ADD,
        self.byte_INPLACE_AND,
        self.byte_INPLACE_FLOOR_DIVIDE,
        self.byte_INPLACE_LSHIFT,
        self.byte_INPLACE_MATRIX_MULTIPLY,
        self.byte_INPLACE_MULTIPLY,
        self.byte_INPLACE_MODULO,  # NB_INPLACE_REMAINDER in 3.11
        self.byte_INPLACE_OR,
        self.byte_INPLACE_POWER,
        self.byte_INPLACE_RSHIFT,
        self.byte_INPLACE_SUBTRACT,
        self.byte_INPLACE_TRUE_DIVIDE,
        self.byte_INPLACE_XOR,
    ]
    binop = binops[opcode.arg]
    binop(opcode)

  # ---------------------------------------------------------------
  # Build and extend collections

  def _build_collection_from_stack(
      self,
      opcode,
      typ: type[Any],
      factory: type[abstract.PythonConstant] = abstract.PythonConstant,
  ) -> None:
    """Pop elements off the stack and build a python constant."""
    count = opcode.arg
    elements = self._stack.popn(count)
    constant = factory(self._ctx, typ(elements))
    self._stack.push(constant.to_variable())

  def byte_BUILD_TUPLE(self, opcode):
    self._build_collection_from_stack(opcode, tuple, factory=abstract.Tuple)

  def byte_BUILD_LIST(self, opcode):
    self._build_collection_from_stack(opcode, list, factory=abstract.List)

  def byte_BUILD_SET(self, opcode):
    self._build_collection_from_stack(opcode, set, factory=abstract.Set)

  def byte_BUILD_MAP(self, opcode):
    n_elts = opcode.arg
    args = self._stack.popn(2 * n_elts)
    ret = {args[2 * i]: args[2 * i + 1] for i in range(n_elts)}
    ret = abstract.Dict(self._ctx, ret)
    self._stack.push(ret.to_variable())

  def byte_BUILD_CONST_KEY_MAP(self, opcode):
    n_elts = opcode.arg
    keys = self._stack.pop()
    # Note that `keys` is a tuple of raw python values; we do not convert them
    # to abstract objects because they are used internally to construct function
    # call args.
    keys = abstract.get_atomic_constant(keys, tuple)
    assert len(keys) == n_elts
    vals = self._stack.popn(n_elts)
    ret = dict(zip(keys, vals))
    ret = abstract.Dict(self._ctx, ret)
    self._stack.push(ret.to_variable())

  def byte_LIST_APPEND(self, opcode):
    # Used by the compiler e.g. for [x for x in ...]
    count = opcode.arg
    val = self._stack.pop()
    # LIST_APPEND peeks back `count` elements in the stack and modifies the list
    # stored there.
    target_var = self._stack.peek(count)
    # We should only have one binding; the target is generated by the compiler.
    target = target_var.get_atomic_value()
    self._replace_atomic_stack_value(count, target.append(val))

  def byte_SET_ADD(self, opcode):
    # Used by the compiler e.g. for {x for x in ...}
    count = opcode.arg
    val = self._stack.pop()
    target_var = self._stack.peek(count)
    target = target_var.get_atomic_value()
    self._replace_atomic_stack_value(count, target.add(val))

  def byte_MAP_ADD(self, opcode):
    # Used by the compiler e.g. for {x, y for x, y in ...}
    count = opcode.arg
    # The value is at the top of the stack, followed by the key.
    key, val = self._stack.popn(2)
    target_var = self._stack.peek(count)
    target = target_var.get_atomic_value()
    self._replace_atomic_stack_value(count, target.setitem(key, val))

  def _unpack_list_extension(self, var: _Var) -> abstract.List:
    try:
      val = var.get_atomic_value()
    except ValueError:
      # This list has multiple possible values, so it is no longer a constant.
      return abstract.List(
          self._ctx, [abstract.Splat.any(self._ctx).to_variable()]
      )
    if isinstance(val, abstract.List):
      return val
    else:
      return abstract.List(
          self._ctx, [abstract.Splat(self._ctx, val).to_variable()]
      )

  def byte_LIST_EXTEND(self, opcode):
    count = opcode.arg
    update_var = self._stack.pop()
    update = self._unpack_list_extension(update_var)
    target_var = self._stack.peek(count)
    target = target_var.get_atomic_value()
    self._replace_atomic_stack_value(count, target.extend(update))

  def _unpack_dict_update(self, var: _Var) -> abstract.Dict | None:
    try:
      val = var.get_atomic_value()
    except ValueError:
      return None
    if isinstance(val, abstract.Dict):
      return val
    elif isinstance(val, abstract.FunctionArgDict):
      if val.indefinite:
        return None
      return abstract.Dict.from_function_arg_dict(self._ctx, val)
    elif abstract.is_any(val):
      return None
    elif isinstance(val, abstract.BaseInstance):
      # This is an object with no concrete python value
      return None
    else:
      raise ValueError('Unexpected dict update:', val)

  def byte_DICT_MERGE(self, opcode):
    # DICT_MERGE is like DICT_UPDATE but raises an exception for duplicate keys.
    self.byte_DICT_UPDATE(opcode)

  def byte_DICT_UPDATE(self, opcode):
    count = opcode.arg
    update_var = self._stack.pop()
    update = self._unpack_dict_update(update_var)
    target_var = self._stack.peek(count)
    target = target_var.get_atomic_value()
    if update is None:
      # The update var has multiple possible values, or no constant, so we
      # cannot merge it into the constant dict. We also don't know if existing
      # items have been overwritten, so we need to return a new 'any' dict.
      ret = self._ctx.types[dict].instantiate()
    else:
      ret = target.update(update)
    self._replace_atomic_stack_value(count, ret)

  def _list_to_tuple(self, var: _Var) -> _Var:
    target = abstract.get_atomic_constant(var, list)
    return abstract.Tuple(self._ctx, tuple(target)).to_variable()

  def byte_LIST_TO_TUPLE(self, opcode):
    self._stack.push(self._list_to_tuple(self._stack.pop()))

  def byte_FORMAT_VALUE(self, opcode):
    if opcode.arg & pyc_marshal.Flags.FVS_MASK:
      self._stack.pop_and_discard()
    # FORMAT_VALUE pops, formats and pushes back a string, so we just need to
    # push a new string onto the stack.
    self._stack.pop_and_discard()
    ret = self._ctx.types[str].instantiate().to_variable()
    self._stack.push(ret)

  def byte_BUILD_STRING(self, opcode):
    # Pop n arguments off the stack and build a string out of them
    self._stack.popn(opcode.arg)
    ret = self._ctx.types[str].instantiate().to_variable()
    self._stack.push(ret)

  # ---------------------------------------------------------------
  # Branches and jumps

  def byte_POP_JUMP_FORWARD_IF_FALSE(self, opcode):
    self._pop_jump_if_false(opcode)

  def byte_POP_JUMP_IF_FALSE(self, opcode):
    self._pop_jump_if_false(opcode)

  def byte_JUMP_FORWARD(self, opcode):
    self._merge_state_into(self._current_state, opcode.argval)

  # ---------------------------------------------------------------
  # Stack manipulation

  def byte_POP_TOP(self, opcode):
    del opcode  # unused
    self._stack.pop_and_discard()

  def byte_DUP_TOP(self, opcode):
    del opcode  # unused
    self._stack.push(self._stack.top())

  def byte_DUP_TOP_TWO(self, opcode):
    del opcode  # unused
    a, b = self._stack.popn(2)
    for v in (a, b, a, b):
      self._stack.push(v)

  def byte_ROT_TWO(self, opcode):
    del opcode  # unused
    self._stack.rotn(2)

  def byte_ROT_THREE(self, opcode):
    del opcode  # unused
    self._stack.rotn(3)

  def byte_ROT_FOUR(self, opcode):
    del opcode  # unused
    self._stack.rotn(4)

  def byte_ROT_N(self, opcode):
    self._stack.rotn(opcode.arg)

  # ---------------------------------------------------------------
  # Intrinsic function calls

  def _call_intrinsic(self, opcode):
    try:
      intrinsic_impl = getattr(self, f'byte_intrinsic_{opcode.argval}')
    except AttributeError as e:
      raise NotImplementedError(
          f'Intrinsic function {opcode.argval} not implemented'
      ) from e
    intrinsic_impl()

  def byte_CALL_INTRINSIC_1(self, opcode):
    self._call_intrinsic(opcode)

  def byte_CALL_INTRINSIC_2(self, opcode):
    self._call_intrinsic(opcode)

  def byte_intrinsic_INTRINSIC_LIST_TO_TUPLE(self):
    self._stack.push(self._list_to_tuple(self._stack.pop()))
