"""Utilities used in vm.py."""

import abc
import collections.abc
from collections.abc import Sequence
import dataclasses
import enum
import itertools
import logging
import re
import reprlib

from pytype import overriding_checks
from pytype import state as frame_state
from pytype import utils
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.blocks import blocks
from pytype.errors import error_types
from pytype.overlays import metaclass
from pytype.pyc import opcodes
from pytype.pyc import pyc
from pytype.pytd import mro
from pytype.pytd import pytd
from pytype.pytd import slots
from pytype.typegraph import cfg

log = logging.getLogger(__name__)

# Create a repr that won't overflow.
_TRUNCATE = 120
_TRUNCATE_STR = 72
_repr_obj = reprlib.Repr()
_repr_obj.maxother = _TRUNCATE
_repr_obj.maxstring = _TRUNCATE_STR
repper = _repr_obj.repr

_FUNCTION_TYPE_COMMENT_RE = re.compile(r"^\((.*)\)\s*->\s*(\S.*?)\s*$")

# Classes supporting pattern matching without an explicit __match_args__
_BUILTIN_MATCHERS = (
    "bool",
    "bytearray",
    "bytes",
    "dict",
    "float",
    "frozenset",
    "int",
    "list",
    "set",
    "str",
    "tuple",
)


class PopBehavior(enum.Enum):
  """Ways in which a JUMP_IF opcode may pop a value off the stack."""

  NONE = enum.auto()  # does not pop
  OR = enum.auto()  # pops when the jump is not taken
  ALWAYS = enum.auto()  # always pops


@dataclasses.dataclass(eq=True, frozen=True)
class _Block:
  type: str
  level: int
  op_index: int

  def __repr__(self):
    return f"Block({self.type}: {self.op_index} level={self.level})"


class FindIgnoredTypeComments(pyc.CodeVisitor):
  """A visitor that finds type comments that will be ignored."""

  def __init__(self, type_comments):
    super().__init__()
    self._type_comments = type_comments
    # Lines will be removed from this set during visiting. Any lines that remain
    # at the end are type comments that will be ignored.
    self._ignored_type_lines = set(type_comments)

  def visit_code(self, code):
    for op in code.code_iter:
      # Make sure we have attached the type comment to an opcode.
      if isinstance(op, blocks.STORE_OPCODES):
        if op.annotation:
          annot = op.annotation
          if self._type_comments.get(op.line) == annot:
            self._ignored_type_lines.discard(op.line)
      elif isinstance(op, opcodes.MAKE_FUNCTION):
        if op.annotation:
          _, line = op.annotation
          self._ignored_type_lines.discard(line)
    return code

  def ignored_lines(self):
    """Returns a set of lines that contain ignored type comments."""
    return self._ignored_type_lines


class FinallyStateTracker:
  """Track return state for try/except/finally blocks."""

  # Used in vm.run_frame()

  RETURN_STATES = ("return", "exception")

  def __init__(self):
    self.stack = []

  def process(self, op, state, ctx) -> str | None:
    """Store state.why, or return it from a stored state."""
    if ctx.vm.is_setup_except(op):
      self.stack.append([op, None])
    if isinstance(op, opcodes.END_FINALLY):
      if self.stack:
        _, why = self.stack.pop()
        if why:
          return why
    elif self.stack and state.why in self.RETURN_STATES:
      self.stack[-1][-1] = state.why

  def check_early_exit(self, state) -> bool:
    """Check if we are exiting the frame from within an except block."""
    return (
        state.block_stack
        and any(x.type == "finally" for x in state.block_stack)
        and state.why in self.RETURN_STATES
    )

  def __repr__(self):
    return repr(self.stack)


class _NameErrorDetails(abc.ABC):
  """Base class for detailed name error messages."""

  @abc.abstractmethod
  def to_error_message(self) -> str:
    ...


class _NameInInnerClassErrorDetails(_NameErrorDetails):

  def __init__(self, attr, class_name):
    super().__init__()
    self._attr = attr
    self._class_name = class_name

  def to_error_message(self):
    return (
        f"Cannot reference {self._attr!r} from class {self._class_name!r} "
        "before the class is fully defined"
    )


class _NameInOuterClassErrorDetails(_NameErrorDetails):
  """Name error details for a name defined in an outer class."""

  def __init__(self, attr, prefix, class_name):
    super().__init__()
    self._attr = attr
    self._prefix = prefix
    self._class_name = class_name

  def to_error_message(self):
    full_attr_name = f"{self._class_name}.{self._attr}"
    if self._prefix:
      full_class_name = f"{self._prefix}.{self._class_name}"
    else:
      full_class_name = self._class_name
    return (
        f"Use {full_attr_name!r} to reference {self._attr!r} from class "
        f"{full_class_name!r}"
    )


class _NameInOuterFunctionErrorDetails(_NameErrorDetails):
  """Name error details for a name defined in an outer function."""

  def __init__(self, attr, outer_scope, inner_scope):
    super().__init__()
    self._attr = attr
    self._outer_scope = outer_scope
    self._inner_scope = inner_scope

  def to_error_message(self):
    keyword = "global" if "global" in self._outer_scope else "nonlocal"
    return (
        f"Add `{keyword} {self._attr}` in {self._inner_scope} to reference "
        f"{self._attr!r} from {self._outer_scope}"
    )


def _get_scopes(
    state,
    names: Sequence[str],
    ctx,
) -> Sequence[abstract.InterpreterClass | abstract.InterpreterFunction]:
  """Gets the class or function objects for a sequence of nested scope names.

  For example, if the code under analysis is:
    class Foo:
      def f(self):
        def g(): ...
  then when called with ['Foo', 'f', 'g'], this method returns
  [InterpreterClass(Foo), InterpreterFunction(f), InterpreterFunction(g)].

  Arguments:
    state: The current state.
    names: A sequence of names for consecutive nested scopes in the module under
      analysis. Must start with a module-level name.
    ctx: The current context.

  Returns:
    The class or function object corresponding to each name in 'names'.
  """
  scopes = []
  for name in names:
    prev = scopes[-1] if scopes else None
    if not prev:
      try:
        _, var = ctx.vm.load_global(state, name)
      except KeyError:
        break
    elif isinstance(prev, abstract.InterpreterClass):
      if name in prev.members:
        var = prev.members[name]
      else:
        break
    else:
      assert isinstance(prev, abstract.InterpreterFunction)
      # For last_frame to be populated, 'prev' has to have been called at
      # least once. This has to be true for all functions except the innermost
      # one, since pytype cannot detect a nested function without analyzing
      # the code that defines the nested function.
      if prev.last_frame and name in prev.last_frame.f_locals.pyval:
        var = prev.last_frame.f_locals.pyval[name]
      else:
        break
    try:
      scopes.append(
          abstract_utils.get_atomic_value(
              var, (abstract.InterpreterClass, abstract.InterpreterFunction)
          )
      )
    except abstract_utils.ConversionError:
      break
  return scopes


def get_name_error_details(state, name: str, ctx) -> _NameErrorDetails | None:
  """Gets a detailed error message for [name-error]."""
  # 'name' is not defined in the current scope. To help the user better
  # understand UnboundLocalError and other similarly confusing errors, we look
  # for definitions of 'name' in outer scopes so we can print a more
  # informative error message.

  # Starting from the current (innermost) frame and moving outward, pytype
  # represents any classes with their own frames until it hits the first
  # function. It represents that function with its own frame and all remaining
  # frames with a single SimpleFrame. For example, if we have:
  #   def f():
  #     class C:
  #       def g():
  #         class D:
  #           class E:
  # then self.frames looks like:
  #   [SimpleFrame(), Frame(f.<locals>.C.g), Frame(D), Frame(E)]
  class_frames = []
  first_function_frame = None
  for frame in reversed(ctx.vm.frames):
    if not frame.func:
      break
    if frame.func.data.is_class_builder:
      class_frames.append(frame)
    else:
      first_function_frame = frame
      break

  # Nested function names include ".<locals>" after each outer function.
  clean = lambda func_name: func_name.replace(".<locals>", "")

  if first_function_frame:
    # Functions have fully qualified names, so we can use the name of
    # first_function_frame to look up the remaining frames.
    parts = clean(first_function_frame.func.data.name).split(".")
    if first_function_frame is ctx.vm.frame:
      parts = parts[:-1]
  else:
    parts = []

  # Check if 'name' is defined in one of the outer classes and functions.
  # Scope 'None' represents the global scope.
  prefix, class_name_parts = None, []
  for scope in itertools.chain(
      reversed(_get_scopes(state, parts, ctx)), [None]
  ):  # pytype: disable=wrong-arg-types
    if class_name_parts:
      # We have located a class that 'name' is defined in and are now
      # constructing the name by which the class should be referenced.
      if isinstance(scope, abstract.InterpreterClass):
        class_name_parts.append(scope.name)
      elif scope:
        prefix = clean(scope.name)
        break
    elif isinstance(scope, abstract.InterpreterClass):
      if name in scope.members:
        # The user may have intended to reference <Class>.<name>
        class_name_parts.append(scope.name)
    else:
      outer_scope = None
      if scope:
        # 'name' is defined in an outer function but not accessible, so it
        # must be redefined in the current frame (an UnboundLocalError).
        # Note that it is safe to assume that annotated_locals corresponds to
        # 'scope' (rather than a different function with the same name) only
        # when 'last_frame' is empty, since the latter being empty means that
        # 'scope' is actively under analysis.
        if (scope.last_frame and name in scope.last_frame.f_locals.pyval) or (
            not scope.last_frame
            and name in ctx.vm.annotated_locals[scope.name.rsplit(".", 1)[-1]]
        ):
          outer_scope = f"function {clean(scope.name)!r}"
      else:
        try:
          _ = ctx.vm.load_global(state, name)
        except KeyError:
          pass
        else:
          outer_scope = "global scope"
      if outer_scope:
        if not ctx.vm.frame.func.data.is_class_builder:
          inner_scope = f"function {clean(ctx.vm.frame.func.data.name)!r}"
        elif ctx.python_version >= (3, 11):
          inner_scope = f"class {clean(class_frames[0].func.data.name)!r}"
        else:
          class_name = ".".join(
              parts
              + [
                  class_frame.func.data.name
                  for class_frame in reversed(class_frames)
              ]
          )
          inner_scope = f"class {class_name!r}"
        return _NameInOuterFunctionErrorDetails(name, outer_scope, inner_scope)
  if class_name_parts:
    return _NameInOuterClassErrorDetails(
        name, prefix, ".".join(reversed(class_name_parts))
    )

  # Check if 'name' is defined in one of the classes with their own frames.
  if class_frames:
    for i, frame in enumerate(class_frames[1:]):
      if ctx.python_version >= (3, 11):
        short_name = frame.func.data.name.rsplit(".", 1)[-1]
      else:
        short_name = frame.func.data.name
      if name in ctx.vm.annotated_locals[short_name]:
        if ctx.python_version >= (3, 11):
          class_name = clean(frame.func.data.name)
        else:
          class_parts = [
              part.func.data.name for part in reversed(class_frames[i + 1 :])
          ]
          class_name = ".".join(parts + class_parts)
        return _NameInInnerClassErrorDetails(name, class_name)
  return None


def log_opcode(op, state, frame, stack_size):
  """Write a multi-line log message, including backtrace and stack."""
  if not log.isEnabledFor(logging.INFO):
    return
  if isinstance(op, (opcodes.CACHE, opcodes.PRECALL)):
    # Do not log NOP-like compiler optimisation opcodes.
    return
  indent = " > " * (stack_size - 1)
  stack_rep = repper(state.data_stack)
  block_stack_rep = repper(state.block_stack)
  if frame.module_name:
    name = frame.f_code.name
    log.info(
        "%s | index: %d, %r, module: %s line: %d",
        indent,
        op.index,
        name,
        frame.module_name,
        op.line,
    )
  else:
    log.info("%s | index: %d, line: %d", indent, op.index, op.line)
  log.info("%s | data_stack: %s", indent, stack_rep)
  log.info("%s | data_stack: %s", indent, [x.data for x in state.data_stack])
  log.info("%s | block_stack: %s", indent, block_stack_rep)
  log.info("%s | node: <%d>%s", indent, state.node.id, state.node.name)
  log.info("%s ## %s", indent, utils.maybe_truncate(str(op), _TRUNCATE))


def _process_base_class(node, base, ctx):
  """Process a base class for InterpreterClass creation."""
  new_base = ctx.program.NewVariable()
  for b in base.bindings:
    base_val = b.data
    if isinstance(b.data, abstract.AnnotationContainer):
      base_val = base_val.base_cls
    if isinstance(base_val, abstract.Union):
      # Union[A,B,...] is a valid base class, but we need to flatten it into a
      # single base variable.
      for o in base_val.options:
        new_base.AddBinding(o, {b}, node)
    else:
      new_base.AddBinding(base_val, {b}, node)
  base = new_base
  if not any(
      isinstance(t, (abstract.Class, abstract.AMBIGUOUS_OR_EMPTY))
      for t in base.data
  ):
    ctx.errorlog.base_class_error(ctx.vm.frames, base)
  return base


def _filter_out_metaclasses(bases, ctx):
  """Process the temporary classes created by six.with_metaclass.

  six.with_metaclass constructs an anonymous class holding a metaclass and a
  list of base classes; if we find instances in `bases`, store the first
  metaclass we find and remove all metaclasses from `bases`.

  Args:
    bases: The list of base classes for the class being constructed.
    ctx: The current context.

  Returns:
    A tuple of (metaclass, base classes)
  """
  non_meta = []
  meta = None
  for base in bases:
    with_metaclass = False
    for b in base.data:
      if isinstance(b, metaclass.WithMetaclassInstance):
        with_metaclass = True
        if not meta:
          # Only the first metaclass gets applied.
          meta = b.cls.to_variable(ctx.root_node)
        non_meta.extend(b.bases)
    if not with_metaclass:
      non_meta.append(base)
  return meta, non_meta


def _expand_generic_protocols(node, bases, ctx):
  """Expand Protocol[T, ...] to Protocol, Generic[T, ...]."""
  expanded_bases = []
  for base in bases:
    if any(abstract_utils.is_generic_protocol(b) for b in base.data):
      protocol_base = ctx.program.NewVariable()
      generic_base = ctx.program.NewVariable()
      generic_cls = ctx.convert.lookup_value("typing", "Generic")
      for b in base.bindings:
        if abstract_utils.is_generic_protocol(b.data):
          protocol_base.AddBinding(b.data.base_cls, {b}, node)
          generic_base.AddBinding(
              abstract.ParameterizedClass(
                  generic_cls,
                  b.data.formal_type_parameters,
                  ctx,
                  b.data.template,
              ),
              {b},
              node,
          )
        else:
          protocol_base.PasteBinding(b)
      expanded_bases.append(generic_base)
      expanded_bases.append(protocol_base)
    else:
      expanded_bases.append(base)
  return expanded_bases


def _check_final_members(cls, class_dict, ctx):
  """Check if the new class overrides a final attribute or method."""
  methods = set(class_dict)
  for base in cls.mro[1:]:
    if not isinstance(base, abstract.Class):
      continue
    if isinstance(base, abstract.PyTDClass):
      # TODO(mdemello): Unify this with IntepreterClass
      for m in methods:
        member = base.final_members.get(m)
        if isinstance(member, pytd.Function):
          ctx.errorlog.overriding_final_method(ctx.vm.frames, cls, base, m)
        elif member:
          ctx.errorlog.overriding_final_attribute(ctx.vm.frames, cls, base, m)
    else:
      for m in methods:
        if m in base.members:
          if any(x.final for x in base.members[m].data):
            ctx.errorlog.overriding_final_method(ctx.vm.frames, cls, base, m)
          ann = base.get_annotated_local(m)
          if ann and ann.final:
            ctx.errorlog.overriding_final_attribute(ctx.vm.frames, cls, base, m)
    methods.update(base.get_own_attributes())


def make_class(node, props, ctx):
  """Create a class with the name, bases and methods given.

  Args:
    node: The current CFG node.
    props: class_mixin.ClassBuilderProperties required to build the class
    ctx: The current context.

  Returns:
    A node and an instance of class_type.
  """
  name = abstract_utils.get_atomic_python_constant(props.name_var)
  log.info("Declaring class %s", name)
  try:
    class_dict = abstract_utils.get_atomic_value(props.class_dict_var)
  except abstract_utils.ConversionError:
    log.error("Error initializing class %r", name)
    return ctx.convert.create_new_unknown(node)
  # Handle six.with_metaclass.
  metacls, bases = _filter_out_metaclasses(props.bases, ctx)
  cls_var = metacls if metacls else props.metaclass_var
  # Flatten Unions in the bases
  bases = [_process_base_class(node, base, ctx) for base in bases]
  # Expand Protocol[T, ...] to Protocol, Generic[T, ...]
  bases = _expand_generic_protocols(node, bases, ctx)
  if not bases:
    # A parent-less class inherits from classobj in Python 2 and from object
    # in Python 3.
    base = ctx.convert.object_type
    bases = [base.to_variable(ctx.root_node)]
  if isinstance(class_dict, abstract.Unsolvable) or not isinstance(
      class_dict, abstract.PythonConstant
  ):
    # An unsolvable appears here if the vm hit maximum depth and gave up on
    # analyzing the class we're now building. Otherwise, if class_dict isn't
    # a constant, then it's an abstract dictionary, and we don't have enough
    # information to continue building the class.
    var = ctx.new_unsolvable(node)
  else:
    if cls_var is None:
      cls_var = class_dict.members.get("__metaclass__")
      if cls_var:
        # This way of declaring metaclasses no longer works in Python 3.
        ctx.errorlog.ignored_metaclass(
            ctx.vm.frames,
            name,
            cls_var.data[0].full_name if cls_var.bindings else "Any",
        )
    if cls_var and all(
        v.data.full_name == "builtins.type" for v in cls_var.bindings
    ):
      cls_var = None
    # pylint: disable=g-long-ternary
    cls = (
        abstract_utils.get_atomic_value(cls_var, default=ctx.convert.unsolvable)
        if cls_var
        else None
    )
    if (
        "__annotations__" not in class_dict.members
        and name in ctx.vm.annotated_locals
    ):
      # Stores type comments in an __annotations__ member as if they were
      # PEP 526-style variable annotations, so that we can type-check
      # attribute assignments.
      annotations_dict = ctx.vm.annotated_locals[name]
      if any(local.typ for local in annotations_dict.values()):
        annotations_member = abstract.AnnotationsDict(
            annotations_dict, ctx
        ).to_variable(node)
        class_dict.members["__annotations__"] = annotations_member
        class_dict.pyval["__annotations__"] = annotations_member
    if "__init_subclass__" in class_dict.members:
      # __init_subclass__ is automatically promoted to a classmethod
      underlying = class_dict.pyval["__init_subclass__"]
      _, method = ctx.vm.load_special_builtin("classmethod").call(
          node, func=None, args=function.Args(posargs=(underlying,))
      )
      class_dict.pyval["__init_subclass__"] = method
    try:
      class_type = props.class_type or abstract.InterpreterClass
      assert issubclass(class_type, abstract.InterpreterClass)
      val = class_type(
          name,
          bases,
          class_dict.pyval,
          cls,
          ctx.vm.current_opcode,
          props.undecorated_methods,
          ctx,
      )
      _check_final_members(val, class_dict.pyval, ctx)
      overriding_checks.check_overriding_members(
          val, bases, class_dict.pyval, ctx.matcher(node), ctx
      )
      val.decorators = props.decorators or []
    except mro.MROError as e:
      ctx.errorlog.mro_error(ctx.vm.frames, name, e.mro_seqs)
      var = ctx.new_unsolvable(node)
    except abstract_utils.GenericTypeError as e:
      ctx.errorlog.invalid_annotation(ctx.vm.frames, e.annot, e.error)
      var = ctx.new_unsolvable(node)
    else:
      var = props.new_class_var or ctx.program.NewVariable()
      var.AddBinding(val, props.class_dict_var.bindings, node)
      node = val.call_metaclass_init(node)
      node = val.call_init_subclass(node)
  ctx.vm.trace_opcode(None, name, var)
  return node, var


def _check_defaults(node, method, ctx):
  """Check parameter defaults against annotations."""
  if not method.signature.has_param_annotations:
    return
  _, args = ctx.vm.create_method_arguments(node, method, use_defaults=True)
  try:
    _, errors = function.match_all_args(ctx, node, method, args)
  except error_types.FailedFunctionCall as e:
    raise AssertionError(
        "Unexpected argument matching error: %s" % e.__class__.__name__
    ) from e
  for e, arg_name, value in errors:
    bad_param = e.bad_call.bad_param
    expected_type = bad_param.typ
    if value == ctx.convert.ellipsis:
      # `...` should be a valid default parameter value for overloads.
      # Unfortunately, the is_overload attribute is not yet set when
      # _check_defaults runs, so we instead check that the method body is empty.
      # As a side effect, `...` is allowed as a default value for any method
      # that does nothing except return None.
      should_report = not method.has_empty_body()
    else:
      should_report = True
    if should_report:
      ctx.errorlog.annotation_type_mismatch(
          ctx.vm.frames,
          expected_type,
          value.to_binding(node),
          arg_name,
          bad_param.error_details,
      )


def make_function(
    name,
    node,
    code,
    globs,
    defaults,
    kw_defaults,
    closure,
    annotations,
    opcode,
    ctx,
):
  """Create a function or closure given the arguments."""
  if closure:
    closure = tuple(
        c for c in abstract_utils.get_atomic_python_constant(closure)
    )
    log.info("closure: %r", closure)
  if not name:
    name = abstract_utils.get_atomic_python_constant(code).qualname
  if not name:
    name = "<lambda>"
  val = abstract.InterpreterFunction.make(
      name,
      def_opcode=opcode,
      code=abstract_utils.get_atomic_python_constant(code),
      f_locals=ctx.vm.frame.f_locals,
      f_globals=globs,
      defaults=defaults,
      kw_defaults=kw_defaults,
      closure=closure,
      annotations=annotations,
      ctx=ctx,
  )
  var = ctx.program.NewVariable()
  var.AddBinding(val, code.bindings, node)
  _check_defaults(node, val, ctx)
  if val.signature.annotations:
    ctx.vm.functions_type_params_check.append(
        (val, ctx.vm.frame.current_opcode)
    )
  return var


def update_excluded_types(node, ctx):
  """Update the excluded_types attribute of functions in the current frame."""
  if not ctx.vm.frame.func:
    return
  func = ctx.vm.frame.func.data
  if isinstance(func, abstract.BoundFunction):
    func = func.underlying
  if not isinstance(func, abstract.InterpreterFunction):
    return
  # If we have code like:
  #   def f(x: T):
  #     def g(x: T): ...
  # then TypeVar T needs to be added to both f and g's excluded_types attribute
  # to avoid 'appears only once in signature' errors for T. Similarly, any
  # TypeVars that appear in variable annotations in a function body also need to
  # be added to excluded_types.
  for functions in ctx.vm.frame.functions_created_in_frame.values():
    for f in functions:
      f.signature.excluded_types |= func.signature.type_params
      func.signature.excluded_types |= f.signature.type_params
  for name, local in ctx.vm.current_annotated_locals.items():
    typ = local.get_type(node, name)
    if typ:
      func.signature.excluded_types.update(
          p.name for p in ctx.annotation_utils.get_type_parameters(typ)
      )


def push_block(state, t, level=None, *, index=0):
  if level is None:
    level = len(state.data_stack)
  return state.push_block(_Block(t, level, index))


def _base(cls):
  if isinstance(cls, abstract.ParameterizedClass):
    return cls.base_cls
  return cls


def _overrides(subcls, supercls, attr):
  """Check whether subcls_var overrides or newly defines the given attribute.

  Args:
    subcls: A potential subclass.
    supercls: A potential superclass.
    attr: An attribute name.

  Returns:
    True if subcls_var is a subclass of supercls_var and overrides or newly
    defines the attribute. False otherwise.
  """
  if subcls and supercls and supercls in subcls.mro:
    subcls = _base(subcls)
    supercls = _base(supercls)
    for cls in subcls.mro:
      if cls == supercls:
        break
      if isinstance(cls, mixin.LazyMembers):
        cls.load_lazy_attribute(attr)
      if (
          isinstance(cls, abstract.SimpleValue)
          and attr in cls.members
          and cls.members[attr].bindings
      ):
        return True
  return False


def _call_binop_on_bindings(node, name, xval, yval, ctx):
  """Call a binary operator on two cfg.Binding objects."""
  rname = slots.REVERSE_NAME_MAPPING.get(name)
  if rname and isinstance(xval.data, abstract.AMBIGUOUS_OR_EMPTY):
    # If the reverse operator is possible and x is ambiguous, then we have no
    # way of determining whether __{op} or __r{op}__ is called.  Technically,
    # the result is also unknown if y is ambiguous, but it is almost always
    # reasonable to assume that, e.g., "hello " + y is a string, even though
    # y could define __radd__.
    return node, ctx.program.NewVariable(
        [ctx.convert.unsolvable], [xval, yval], node
    )
  options = [(xval, yval, name)]
  if rname:
    options.append((yval, xval, rname))
    if _overrides(yval.data.cls, xval.data.cls, rname):
      # If y is a subclass of x and defines its own reverse operator, then we
      # need to try y.__r{op}__ before x.__{op}__.
      options.reverse()
  error = None
  for left_val, right_val, attr_name in options:
    if isinstance(left_val.data, abstract.Class) and attr_name == "__getitem__":
      # We're parameterizing a type annotation. Set valself to None to
      # differentiate this action from a real __getitem__ call on the class.
      valself = None
    else:
      valself = left_val
    node, attr_var = ctx.attribute_handler.get_attribute(
        node, left_val.data, attr_name, valself
    )
    if attr_var and attr_var.bindings:
      args = function.Args(posargs=(right_val.AssignToNewVariable(),))
      try:
        return function.call_function(
            ctx,
            node,
            attr_var,
            args,
            fallback_to_unsolvable=False,
            strict_filter=len(attr_var.bindings) > 1,
        )
      except (error_types.DictKeyMissing, error_types.FailedFunctionCall) as e:
        # It's possible that this call failed because the function returned
        # NotImplemented.  See, e.g.,
        # test_operators.ReverseTest.check_reverse(), in which 1 {op} Bar() ends
        # up using Bar.__r{op}__. Thus, we need to save the error and try the
        # other operator.
        if e > error:
          error = e
  if error:
    raise error  # pylint: disable=raising-bad-type
  else:
    return node, None


def _get_annotation(node, var, ctx):
  """Extract an annotation from terms in `a | b | ...`."""
  # Python does not support late annotations in | expressions
  try:
    abstract_utils.get_atomic_python_constant(var, str)
  except abstract_utils.ConversionError:
    pass
  else:
    return None
  with ctx.errorlog.checkpoint() as record:
    annot = ctx.annotation_utils.extract_annotation(
        node, var, "varname", ctx.vm.simple_stack()
    )
  if record.errors:
    return None
  return annot


def _maybe_union(node, x, y, ctx):
  """Attempt to evaluate a '|' operation as a typing.Union."""
  x_annot = _get_annotation(node, x, ctx)
  y_annot = _get_annotation(node, y, ctx)
  opts = [x_annot, y_annot]
  if any(o is None for o in opts):
    return None
  if all(isinstance(o, abstract.AMBIGUOUS) for o in opts):
    # It is ambiguous whether this is a typing.Union or a regular __or__
    # operation. Returning unsolvable is safe either way.
    return ctx.new_unsolvable(node)
  return abstract.Union(opts, ctx).to_variable(node)


def call_binary_operator(state, name, x, y, report_errors, ctx):
  """Map a binary operator to "magic methods" (__add__ etc.)."""
  results = []
  log.debug("Calling binary operator %s", name)
  nodes = []
  error = None
  x = abstract_utils.simplify_variable(x, state.node, ctx)
  y = abstract_utils.simplify_variable(y, state.node, ctx)
  for xval in x.bindings:
    for yval in y.bindings:
      try:
        node, ret = _call_binop_on_bindings(state.node, name, xval, yval, ctx)
      except (error_types.DictKeyMissing, error_types.FailedFunctionCall) as e:
        if (
            report_errors
            and e > error
            and state.node.HasCombination([xval, yval])
        ):
          error = e
      else:
        if ret:
          nodes.append(node)
          results.append(ret)
  # Python 3.10 supports writing union types as A | B | ...
  # Only try this as a fallback otherwise we miss overriding of __or__
  if ctx.python_version >= (3, 10) and name == "__or__":
    fail = (
        error
        or not results
        or isinstance(results[0].data[0], abstract.Unsolvable)
    )
    if fail:
      ret = _maybe_union(state.node, x, y, ctx)
      if ret:
        return state, ret
  if nodes:
    state = state.change_cfg_node(ctx.join_cfg_nodes(nodes))
  result = ctx.join_variables(state.node, results)
  log.debug("Result: %r %r", result, result.data)
  log.debug("Error: %r", error)
  log.debug("Report Errors: %r", report_errors)
  if report_errors:
    if error is None:
      if not result.bindings:
        if ctx.options.report_errors:
          ctx.errorlog.unsupported_operands(ctx.vm.frames, name, x, y)
        result = ctx.new_unsolvable(state.node)
    elif not result.bindings or ctx.options.strict_parameter_checks:
      if ctx.options.report_errors:
        ctx.errorlog.invalid_function_call(ctx.vm.frames, error)
      state, result = error.get_return(state)
  return state, result


def call_inplace_operator(state, iname, x, y, ctx):
  """Try to call a method like __iadd__, possibly fall back to __add__."""
  state, attr = ctx.vm.load_attr_noerror(state, x, iname)
  if attr is None:
    log.info("No inplace operator %s on %r", iname, x)
    name = iname.replace("i", "", 1)  # __iadd__ -> __add__ etc.
    state = state.forward_cfg_node(f"BinOp:{name}")
    state, ret = call_binary_operator(
        state, name, x, y, report_errors=True, ctx=ctx
    )
  else:
    # TODO(b/159039220): If x is a Variable with distinct types, both __add__
    # and __iadd__ might happen.
    try:
      state, ret = ctx.vm.call_function_with_state(
          state, attr, (y,), fallback_to_unsolvable=False
      )
    except error_types.FailedFunctionCall as e:
      ctx.errorlog.invalid_function_call(ctx.vm.frames, e)
      state, ret = e.get_return(state)
  return state, ret


def check_for_deleted(state, name, var, ctx):
  for x in var.Data(state.node):
    if isinstance(x, abstract.Deleted):
      # Referencing a deleted variable
      details = (
          f"\nVariable {name} has been used after it has been deleted"
          f" (line {x.line})."
      )
      ctx.errorlog.name_error(ctx.vm.frames, name, details=details)


def load_closure_cell(state, op, check_bindings, ctx):
  """Retrieve the value out of a closure cell.

  Used to generate the 'closure' tuple for MAKE_CLOSURE.

  Each entry in that tuple is typically retrieved using LOAD_CLOSURE.

  Args:
    state: The current VM state.
    op: The opcode. op.arg is the index of a "cell variable": This corresponds
      to an entry in co_cellvars or co_freevars and is a variable that's bound
      into a closure.
    check_bindings: Whether to check the retrieved value for bindings.
    ctx: The current context.

  Returns:
    A new state.
  """
  cell_index = ctx.vm.frame.f_code.get_cell_index(op.argval)
  cell = ctx.vm.frame.cells[cell_index]
  # If we have closed over a variable in an inner function, then invoked the
  # inner function before the variable is defined, raise a name error here.
  # See test_closures.ClosuresTest.test_undefined_var
  if check_bindings and not cell.bindings:
    ctx.errorlog.name_error(ctx.vm.frames, op.argval)
    cell = ctx.new_unsolvable(state.node)
  visible_bindings = cell.Filter(state.node, strict=False)
  if len(visible_bindings) != len(cell.bindings):
    # We need to filter here because the closure will be analyzed outside of
    # its creating context, when information about what values are visible
    # has been lost.
    new_cell = ctx.program.NewVariable()
    if visible_bindings:
      for b in visible_bindings:
        new_cell.PasteBinding(b, state.node)
    else:
      # See test_closures.ClosuresTest.test_no_visible_bindings.
      new_cell.AddBinding(ctx.convert.unsolvable)
    # Update the cell because the DELETE_DEREF implementation works on
    # variable identity.
    ctx.vm.frame.cells[cell_index] = cell = new_cell
  name = op.argval
  ctx.vm.set_var_name(cell, name)
  check_for_deleted(state, name, cell, ctx)
  ctx.vm.trace_opcode(op, name, cell)
  return state.push(cell)


def jump_if(state, op, ctx, *, jump_if_val, pop=PopBehavior.NONE):
  """Implementation of various _JUMP_IF bytecodes.

  Args:
    state: Initial FrameState.
    op: An opcode.
    ctx: The current context.
    jump_if_val: Indicates what value leads to a jump. The non-jump state is
      reached by the value's negation. Use frame_state.NOT_NONE for `not None`.
    pop: Whether and how the opcode pops a value off the stack.

  Returns:
    The new FrameState.
  """
  # Determine the conditions.  Assume jump-if-true, then swap conditions
  # if necessary.
  if pop is PopBehavior.ALWAYS:
    state, value = state.pop()
  else:
    value = state.top()
  if jump_if_val is None:
    normal_val = frame_state.NOT_NONE
  elif jump_if_val is frame_state.NOT_NONE:
    normal_val = None
  elif isinstance(jump_if_val, bool):
    normal_val = not jump_if_val
  else:
    raise NotImplementedError(f"Unsupported jump value: {jump_if_val!r}")
  jump = frame_state.restrict_condition(state.node, value, jump_if_val)
  normal = frame_state.restrict_condition(state.node, value, normal_val)

  # Jump.
  if jump is not frame_state.UNSATISFIABLE:
    if jump:
      assert jump.binding
      else_state = state.forward_cfg_node(
          "Jump", jump.binding
      ).forward_cfg_node("Jump")
    else:
      else_state = state.forward_cfg_node("Jump")
    ctx.vm.store_jump(op.target, else_state)
  else:
    else_state = None
  # Don't jump.
  if pop is PopBehavior.OR:
    state = state.pop_and_discard()
  if normal is frame_state.UNSATISFIABLE:
    return state.set_why("unsatisfiable")
  elif not else_state and not normal:
    return state  # We didn't actually branch.
  else:
    return state.forward_cfg_node("NoJump", normal.binding if normal else None)


def process_function_type_comment(node, op, func, ctx):
  """Modifies annotations from a function type comment.

  Checks if a type comment is present for the function.  If so, the type
  comment is used to populate annotations.  It is an error to have
  a type comment when annotations is not empty.

  Args:
    node: The current node.
    op: An opcode (used to determine filename and line number).
    func: An abstract.InterpreterFunction.
    ctx: The current context.
  """
  if not op.annotation:
    return

  comment, line = op.annotation

  # It is an error to use a type comment on an annotated function.
  if func.signature.annotations:
    ctx.errorlog.redundant_function_type_comment(op.code.filename, line)
    return

  # Parse the comment, use a fake Opcode that is similar to the original
  # opcode except that it is set to the line number of the type comment.
  # This ensures that errors are printed with an accurate line number.
  fake_stack = ctx.vm.simple_stack(op.at_line(line))
  m = _FUNCTION_TYPE_COMMENT_RE.match(comment)
  if not m:
    ctx.errorlog.invalid_function_type_comment(fake_stack, comment)
    return
  args, return_type = m.groups()
  assert args is not None and return_type is not None

  if args != "...":
    annot = args.strip()
    try:
      ctx.annotation_utils.eval_multi_arg_annotation(
          node, func, annot, fake_stack
      )
    except abstract_utils.ConversionError:
      ctx.errorlog.invalid_function_type_comment(
          fake_stack, annot, details="Must be constant."
      )

  ret = ctx.convert.build_string(None, return_type)
  func.signature.set_annotation(
      "return",
      ctx.annotation_utils.extract_annotation(node, ret, "return", fake_stack),
  )


def _merge_tuple_bindings(var, ctx):
  """Merge a set of heterogeneous tuples from var's bindings."""
  # Helper function for _unpack_iterable. We have already checked that all the
  # tuples are the same length.
  if len(var.bindings) == 1:
    return var
  length = var.data[0].tuple_length
  seq = [ctx.program.NewVariable() for _ in range(length)]
  for tup in var.data:
    for i in range(length):
      seq[i].PasteVariable(tup.pyval[i])
  return seq


# Helper functions for match_* and unpack_iterable
def _var_is_fixed_length_tuple(var: cfg.Variable) -> bool:
  return all(isinstance(d, abstract.Tuple) for d in var.data) and all(
      d.tuple_length == var.data[0].tuple_length for d in var.data
  )


def _var_maybe_unknown(var: cfg.Variable) -> bool:
  return any(isinstance(x, abstract.Unsolvable) for x in var.data) or all(
      isinstance(x, abstract.Unknown) for x in var.data
  )


def _convert_keys(keys_var: cfg.Variable):
  keys = abstract_utils.get_atomic_python_constant(keys_var, tuple)
  return tuple(map(abstract_utils.get_atomic_python_constant, keys))


def match_sequence(obj_var: cfg.Variable) -> bool:
  """See if var is a sequence for pattern matching."""
  return (
      abstract_utils.match_atomic_python_constant(
          obj_var, collections.abc.Iterable
      )
      or abstract_utils.is_var_indefinite_iterable(obj_var)
      or _var_is_fixed_length_tuple(obj_var)
      or _var_maybe_unknown(obj_var)
  )


def match_mapping(node, obj_var: cfg.Variable, ctx) -> bool:
  """See if var is a map for pattern matching."""
  mapping = ctx.convert.lookup_value("typing", "Mapping")
  return (
      abstract_utils.match_atomic_python_constant(
          obj_var, collections.abc.Mapping
      )
      or ctx.matcher(node).compute_one_match(obj_var, mapping).success
      or _var_maybe_unknown(obj_var)
  )


def match_keys(
    node, obj_var: cfg.Variable, keys_var: cfg.Variable, ctx
) -> cfg.Variable | None:
  """Pick values out of a mapping for pattern matching."""
  keys = _convert_keys(keys_var)
  if _var_maybe_unknown(obj_var):
    return ctx.convert.build_tuple(
        node, [ctx.new_unsolvable(node) for _ in keys]
    )
  try:
    mapping = abstract_utils.get_atomic_python_constant(
        obj_var, collections.abc.Mapping
    )
  except abstract_utils.ConversionError:
    # We have an abstract mapping
    ret = [ctx.program.NewVariable() for _ in keys]
    for d in obj_var.data:
      # TODO(mdemello): Do we need to retrieve and call `__getitem__` on
      # every binding of `obj_var` instead?
      v = d.get_instance_type_parameter(abstract_utils.V)
      for x in ret:
        x.PasteVariable(v)
  else:
    # We have a concrete mapping
    try:
      ret = [mapping[k] for k in keys]
    except KeyError:
      return None
  return ctx.convert.build_tuple(node, ret)


@dataclasses.dataclass
class ClassMatch:
  success: bool | None  # tri-state boolean for if-splitting
  values: cfg.Variable | None

  @property
  def matched(self):
    # Either True or None denotes a successful match
    return self.success is not False  # pylint: disable=g-bool-id-comparison


def _match_builtin_class(
    node,
    success: bool | None,
    cls: abstract.Class,
    keys: tuple[str, ...],
    posarg_count: int,
    ctx,
) -> ClassMatch:
  """Match a builtin class with a single posarg constructor."""
  assert success is not False  # pylint: disable=g-bool-id-comparison
  if posarg_count > 1:
    ctx.errorlog.match_posargs_count(ctx.vm.frames, cls, posarg_count, 1)
    return ClassMatch(False, None)
  elif keys:
    # Builtin matchers do not take kwargs
    return ClassMatch(False, None)
  else:
    ret = [cls.instantiate(node)]
    return ClassMatch(success, ctx.convert.build_tuple(node, ret))


def match_class(
    node,
    obj_var: cfg.Variable,
    cls_var: cfg.Variable,
    keys_var: cfg.Variable,
    posarg_count: int,
    ctx,
) -> ClassMatch:
  """Pick attributes out of a class instance for pattern matching."""
  keys = _convert_keys(keys_var)
  try:
    cls = abstract_utils.get_atomic_value(
        cls_var, (abstract.Class, abstract.AnnotationContainer)
    )
  except abstract_utils.ConversionError:
    return ClassMatch(success=None, values=None)
  if isinstance(cls, abstract.AnnotationContainer):
    # Special case typing.* and collections.abc.* classes that get loaded as
    # an AnnotationContainer rather than a Class
    cls = cls.base_cls
  if _var_maybe_unknown(obj_var):
    bindings = ctx.vm.init_class(node, cls).bindings
    success = None
  else:
    # Check both whether any binding of `obj_var` matches, and whether all of
    # them do. If all bindings match then return True since the pattern will
    # always succeed.
    m = ctx.matcher(node).compute_one_match(obj_var, cls, match_all_views=False)
    total = ctx.matcher(node).compute_one_match(
        obj_var, cls, match_all_views=True
    )
    if m.success:
      bindings = list(
          itertools.chain(*map(lambda x: x.view.values(), m.good_matches))
      )
      success = True if total.success else None
    else:
      return ClassMatch(False, None)

  if posarg_count:
    if isinstance(cls, abstract.PyTDClass) and cls.name in _BUILTIN_MATCHERS:
      return _match_builtin_class(node, success, cls, keys, posarg_count, ctx)

    if posarg_count > len(cls.match_args):
      ctx.errorlog.match_posargs_count(
          ctx.vm.frames, cls, posarg_count, len(cls.match_args)
      )
      return ClassMatch(False, None)
    keys = cls.match_args[:posarg_count] + keys
  ret = [ctx.program.NewVariable() for _ in keys]
  for i, k in enumerate(keys):
    for b in bindings:
      _, v = ctx.attribute_handler.get_attribute(node, b.data, k, b)
      if not v:
        # We are missing a key
        return ClassMatch(False, None)
      ret[i].PasteVariable(v)
  return ClassMatch(success, ctx.convert.build_tuple(node, ret))


def copy_dict_without_keys(
    node, obj_var: cfg.Variable, keys_var: cfg.Variable, ctx
) -> cfg.Variable:
  """Create a copy of the input dict with some keys deleted."""
  # NOTE: This handles the specific case of a dict with a concrete pyval, where
  # we can track keys and values. Otherwise just return the object with type
  # unchanged; at worst it will be a superset of the correct type.
  if not all(abstract_utils.is_concrete_dict(x) for x in obj_var.data):
    return obj_var
  keys = _convert_keys(keys_var)
  ret = abstract.Dict(ctx)
  for data in obj_var.data:
    # NOTE: We cannot call `ret.update(data, omit=keys)` because that tries to
    # preserve the type params from `data` even if some of the types were set by
    # omitted keys.
    for k, v in data.items():
      if k not in keys:
        ret.set_str_item(node, k, v)
  return ret.to_variable(node)


def unpack_iterable(node, var, ctx):
  """Unpack an iterable."""
  elements = []
  try:
    itr = abstract_utils.get_atomic_python_constant(
        var, collections.abc.Iterable
    )
  except abstract_utils.ConversionError:
    if abstract_utils.is_var_indefinite_iterable(var):
      elements.append(abstract.Splat(ctx, var).to_variable(node))
    elif _var_is_fixed_length_tuple(var):
      # If we have a set of bindings to tuples all of the same length, treat
      # them as a definite tuple with union-typed fields.
      vs = _merge_tuple_bindings(var, ctx)
      elements.extend(vs)
    elif _var_maybe_unknown(var):
      # If we have an unsolvable or unknown we are unpacking as an iterable,
      # make sure it is treated as a tuple and not a single value.
      v = ctx.convert.tuple_type.instantiate(node)
      elements.append(abstract.Splat(ctx, v).to_variable(node))
    else:
      # If we reach here we have tried to unpack something that wasn't
      # iterable. Wrap it in a splat and let the matcher raise an error.
      elements.append(abstract.Splat(ctx, var).to_variable(node))
  else:
    for v in itr:
      # Some iterable constants (e.g., tuples) already contain variables,
      # whereas others (e.g., strings) need to be wrapped.
      if isinstance(v, cfg.Variable):
        elements.append(v)
      else:
        elements.append(ctx.convert.constant_to_var(v))
  return elements


def pop_and_unpack_list(state, count, ctx):
  """Pop count iterables off the stack and concatenate."""
  state, iterables = state.popn(count)
  elements = []
  for var in iterables:
    elements.extend(unpack_iterable(state.node, var, ctx))
  return state, elements


def merge_indefinite_iterables(node, target, iterables_to_merge):
  for var in iterables_to_merge:
    if abstract_utils.is_var_splat(var):
      for val in abstract_utils.unwrap_splat(var).data:
        p = val.get_instance_type_parameter(abstract_utils.T)
        target.merge_instance_type_parameter(node, abstract_utils.T, p)
    else:
      target.merge_instance_type_parameter(node, abstract_utils.T, var)


def unpack_and_build(state, count, build_concrete, container_type, ctx):
  state, seq = pop_and_unpack_list(state, count, ctx)
  if any(abstract_utils.is_var_splat(x) for x in seq):
    retval = abstract.Instance(container_type, ctx)
    merge_indefinite_iterables(state.node, retval, seq)
    ret = retval.to_variable(state.node)
  else:
    ret = build_concrete(state.node, seq)
  return state.push(ret)


def build_function_args_tuple(node, seq, ctx):
  # If we are building function call args, do not collapse indefinite
  # subsequences into a single tuple[x, ...], but allow them to be concrete
  # elements to match against function parameters and *args.
  tup = ctx.convert.tuple_to_value(seq)
  tup.is_unpacked_function_args = True
  return tup.to_variable(node)


def ensure_unpacked_starargs(node, starargs, ctx):
  """Unpack starargs if it has not been done already."""
  # TODO(mdemello): If we *have* unpacked the arg in a previous opcode will it
  # always have a single binding?
  if not any(
      isinstance(x, abstract.Tuple) and x.is_unpacked_function_args
      for x in starargs.data
  ):
    seq = unpack_iterable(node, starargs, ctx)
    starargs = build_function_args_tuple(node, seq, ctx)
  return starargs


def build_map_unpack(state, arg_list, ctx):
  """Merge a list of kw dicts into a single dict."""
  args = abstract.Dict(ctx)
  for arg in arg_list:
    for data in arg.data:
      args.update(state.node, data)
  args = args.to_variable(state.node)
  return args


def _binding_to_coroutine(state, b, bad_bindings, ret, top, ctx):
  """Helper for _to_coroutine.

  Args:
    state: The current state.
    b: A cfg.Binding.
    bad_bindings: Bindings that are not coroutines.
    ret: A return variable that this helper will add to.
    top: Whether this is the top-level recursive call.
    ctx: The current context.

  Returns:
    The state.
  """
  if b not in bad_bindings:  # this is already a coroutine
    ret.PasteBinding(b)
    return state
  if (
      ctx.matcher(state.node).match_var_against_type(
          b.variable, ctx.convert.generator_type, {}, {b.variable: b}
      )
      is not None
  ):
    # This is a generator; convert it to a coroutine. This conversion is
    # necessary even though generator-based coroutines convert their return
    # values themselves because __await__ can return a generator.
    ret_param = b.data.get_instance_type_parameter(abstract_utils.V)
    coroutine = abstract.Coroutine(ctx, ret_param, state.node)
    ret.AddBinding(coroutine, [b], state.node)
    return state
  # This is neither a coroutine or a generator; call __await__.
  if not top:  # we've already called __await__
    ret.PasteBinding(b)
    return state
  _, await_method = ctx.attribute_handler.get_attribute(
      state.node, b.data, "__await__", b
  )
  if await_method is None or not await_method.bindings:
    # We don't need to log an error here; byte_GET_AWAITABLE will check
    # that the final result is awaitable.
    ret.PasteBinding(b)
    return state
  state, await_obj = ctx.vm.call_function_with_state(state, await_method, ())
  state, subret = to_coroutine(state, await_obj, False, ctx)
  ret.PasteVariable(subret)
  return state


def to_coroutine(state, obj, top, ctx):
  """Convert any awaitables and generators in obj to coroutines.

  Implements the GET_AWAITABLE opcode, which returns obj unchanged if it is a
  coroutine or generator and otherwise resolves obj.__await__
  (https://docs.python.org/3/library/dis.html#opcode-GET_AWAITABLE). So that
  we don't have to handle awaitable generators specially, our implementation
  converts generators to coroutines.

  Args:
    state: The current state.
    obj: The object, a cfg.Variable.
    top: Whether this is the top-level recursive call, to prevent incorrectly
      recursing into the result of obj.__await__.
    ctx: The current context.

  Returns:
    A tuple of the state and a cfg.Variable of coroutines.
  """
  bad_bindings = []
  for b in obj.bindings:
    if (
        ctx.matcher(state.node).match_var_against_type(
            obj, ctx.convert.coroutine_type, {}, {obj: b}
        )
        is None
    ):
      bad_bindings.append(b)
  if not bad_bindings:  # there are no non-coroutines
    return state, obj
  ret = ctx.program.NewVariable()
  for b in obj.bindings:
    state = _binding_to_coroutine(state, b, bad_bindings, ret, top, ctx)
  return state, ret
