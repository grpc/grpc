"""Base support for generating classes from data declarations.

Contains common functionality used by dataclasses, attrs and namedtuples.
"""

import abc
import collections
import dataclasses
import logging
from typing import Any, ClassVar

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.overlays import overlay_utils
from pytype.overlays import special_builtins


log = logging.getLogger(__name__)


# type aliases for convenience
Param = overlay_utils.Param
Attribute = class_mixin.Attribute
AttributeKinds = class_mixin.AttributeKinds


# Probably should make this an enum.Enum at some point.
class Ordering:
  """Possible orderings for get_class_locals."""

  # Order by each variable's first annotation. For example, for
  #   class Foo:
  #     x: int
  #     y: str
  #     x: float
  # the locals will be [(x, Instance(float)), (y, Instance(str))]. Note that
  # unannotated variables will be skipped, and the values of later annotations
  # take precedence over earlier ones.
  FIRST_ANNOTATE = object()
  # Order by each variable's last definition. So for
  #   class Foo:
  #     x = 0
  #     y = 'hello'
  #     x = 4.2
  # the locals will be [(y, Instance(str)), (x, Instance(float))]. Note that
  # variables without assignments will be skipped.
  LAST_ASSIGN = object()


class Decorator(abstract.PyTDFunction, metaclass=abc.ABCMeta):
  """Base class for decorators that generate classes from data declarations."""

  # Defaults for the args that we support (dataclasses only support 'init',
  # but the others default to false so they should not affect anything).
  DEFAULT_ARGS: ClassVar[dict[str, Any]] = {
      "init": True,
      "kw_only": False,
      "auto_attribs": False,
  }

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    # Decorator.call() is invoked first with args, then with the class to
    # decorate, so we need to first store the args and then associate them to
    # the right class.
    self._current_args = None
    # Some constructors like attr.dataclass partially apply args, overriding the
    # defaults attached to the class.
    self.partial_args = {}
    self.args = {}  # map from each class we decorate to its args

  @abc.abstractmethod
  def decorate(self, node, cls):
    """Apply the decorator to cls."""

  def get_initial_args(self):
    ret = self.DEFAULT_ARGS.copy()
    ret.update(self.partial_args)
    return ret

  def update_kwargs(self, args):
    """Update current_args with the Args passed to the decorator."""
    self._current_args = self.get_initial_args()
    for k, v in args.namedargs.items():
      if k in self._current_args:
        try:
          self._current_args[k] = abstract_utils.get_atomic_python_constant(v)
        except abstract_utils.ConversionError:
          self.ctx.errorlog.not_supported_yet(
              self.ctx.vm.frames, f"Non-constant argument to decorator: {k!r}"
          )

  def set_current_args(self, kwargs):
    """Set current_args when constructing a class directly."""
    self._current_args = self.get_initial_args()
    self._current_args.update(kwargs)

  def init_name(self, attr):
    """Attribute name as an __init__ keyword, could differ from attr.name."""
    return attr.name

  def make_init(self, node, cls, attrs, init_method_name="__init__"):
    pos_params = []
    kwonly_params = []
    all_kwonly = self.args[cls]["kw_only"]
    for attr in attrs:
      if not attr.init:
        continue
      typ = attr.init_type or attr.typ
      # call self.init_name in case the name differs from the field name -
      # e.g. attrs removes leading underscores from attrib names when
      # generating kwargs for __init__.
      param = Param(name=self.init_name(attr), typ=typ, default=attr.default)

      # kw_only=False in a field does not override kw_only=True in the class.
      if all_kwonly or attr.kw_only:
        kwonly_params.append(param)
      else:
        pos_params.append(param)

    return overlay_utils.make_method(
        self.ctx, node, init_method_name, pos_params, 0, kwonly_params
    )

  def call(self, node, func, args, alias_map=None):
    """Construct a decorator, and call it on the class."""
    args = args.simplify(node, self.ctx)
    self.match_args(node, args)

    # There are two ways to use a decorator:
    #   @decorator(...)
    #   class Foo: ...
    # or
    #   @decorator
    #   class Foo: ...
    # In the first case, call() is invoked twice: once with kwargs to create the
    # decorator object and once with the decorated class as a posarg. So we call
    # update_kwargs on the first invocation, setting _current_args, and skip it
    # on the second.
    # In the second case, we call update_kwargs on the first and only
    # invocation.
    if not self._current_args:
      self.update_kwargs(args)

    # NOTE: @dataclass is py3-only and has explicitly kwonly args in its
    # constructor.
    #
    # @attr.s does not take positional arguments in typical usage, but
    # technically this works:
    #   class Foo:
    #     x = attr.ib()
    #   Foo = attr.s(Foo, **kwargs)
    #
    # Unfortunately, it also works to pass kwargs as posargs; we will at least
    # reject posargs if the first arg is not a Callable.
    if not args.posargs:
      return node, self.to_variable(node)

    cls_var = args.posargs[0]
    # We should only have a single binding here
    (cls,) = cls_var.data

    if not isinstance(cls, abstract.Class):
      # There are other valid types like abstract.Unsolvable that we don't need
      # to do anything with.
      return node, cls_var

    self.args[cls] = self._current_args
    # Reset _current_args so we don't use old args for a new class.
    self._current_args = None

    # decorate() modifies the cls object in place
    self.decorate(node, cls)
    return node, cls_var


class FieldConstructor(abstract.PyTDFunction):
  """Implements constructors for fields."""

  def get_kwarg(self, args, name, default):
    if name not in args.namedargs:
      return default
    try:
      return abstract_utils.get_atomic_python_constant(args.namedargs[name])
    except abstract_utils.ConversionError:
      self.ctx.errorlog.not_supported_yet(
          self.ctx.vm.frames, f"Non-constant argument {name!r}"
      )

  def get_positional_names(self):
    # TODO(mdemello): We currently assume all field constructors are called with
    # namedargs, which has worked in practice but is not required by the attrs
    # or dataclasses apis.
    return []


def is_method(var):
  if var is None:
    return False
  return isinstance(
      var.data[0],
      (
          abstract.INTERPRETER_FUNCTION_TYPES,
          special_builtins.ClassMethodInstance,
          special_builtins.PropertyInstance,
          special_builtins.StaticMethodInstance,
      ),
  )


def is_dunder(name):
  return name.startswith("__") and name.endswith("__")


def add_member(node, cls, name, typ):
  if typ.formal:
    # If typ contains a type parameter, we mark it as empty so that instances
    # will use __annotations__ to fill in concrete type parameter values.
    instance = typ.ctx.convert.empty.to_variable(node)
  else:
    # See test_attr.TestAttrib.test_repeated_default - keying on the name
    # prevents attributes from sharing the same default object.
    instance = typ.ctx.vm.init_class(node, typ, extra_key=name)
  cls.members[name] = instance


def is_relevant_class_local(
    class_local: abstract_utils.Local,
    class_local_name: str,
    allow_methods: bool,
):
  """Tests whether the current class local could be relevant for type checking.

  For example, this doesn't match __dunder__ class locals.

  To get an abstract_utils.Local from a vm.LocalOps, you can use,
  'vm_instance.annotated_locals[cls_name][op.name]'.

  Args:
    class_local: the local to query
    class_local_name: the name of the class local (because abstract_utils.Local
      does not hold this information).
    allow_methods: whether to allow methods class locals to match

  Returns:
    Whether this class local could possibly be relevant for type checking.
      Callers will usually want to filter even further.
  """
  if is_dunder(class_local_name):
    return False
  if not allow_methods and not class_local.typ and is_method(class_local.orig):
    return False
  return True


def get_class_locals(cls_name: str, allow_methods: bool, ordering, ctx):
  """Gets a dictionary of the class's local variables.

  Args:
    cls_name: The name of an abstract.InterpreterClass.
    allow_methods: A bool, whether to allow methods as variables.
    ordering: A classgen.Ordering describing the order in which the variables
      should appear.
    ctx: The abstract context.

  Returns:
    A collections.OrderedDict of the locals.
  """
  out = collections.OrderedDict()
  if cls_name not in ctx.vm.local_ops:
    # See TestAttribPy3.test_cannot_decorate in tests/test_attr2.py. The
    # class will not be in local_ops if a previous decorator hides it.
    return out
  for op in ctx.vm.local_ops[cls_name]:
    local = ctx.vm.annotated_locals[cls_name][op.name]
    if not is_relevant_class_local(local, op.name, allow_methods):
      continue
    if ordering is Ordering.FIRST_ANNOTATE:
      if not op.is_annotate() or op.name in out:
        continue
    else:
      assert ordering is Ordering.LAST_ASSIGN
      if not op.is_assign():
        continue
      elif op.name in out:
        out.move_to_end(op.name)
    out[op.name] = local
  return out


def make_replace_method(ctx, node, cls, *, kwargs_name="kwargs"):
  """Create a replace() method for a dataclass."""
  # This is used by several packages that extend dataclass.
  # The signature is
  #   def replace(self: T, **kwargs) -> T
  typevar = abstract.TypeParameter(abstract_utils.T + cls.name, ctx, bound=cls)
  return overlay_utils.make_method(
      ctx=ctx,
      node=node,
      name="replace",
      return_type=typevar,
      self_param=overlay_utils.Param("self", typevar),
      kwargs=overlay_utils.Param(kwargs_name),
  )


def get_or_create_annotations_dict(members, ctx):
  """Get __annotations__ from members map, create and attach it if not present.

  The returned dict is also referenced by members, so it is safe to mutate.

  Args:
    members: A dict of member name to variable.
    ctx: context.Context instance.

  Returns:
    members['__annotations__'] unpacked as a python dict
  """
  annotations_dict = abstract_utils.get_annotations_dict(members)
  if annotations_dict is None:
    annotations_dict = abstract.AnnotationsDict({}, ctx)
    members["__annotations__"] = annotations_dict.to_variable(ctx.root_node)
  return annotations_dict


@dataclasses.dataclass
class Field:
  """A class member variable."""

  name: str
  typ: Any
  default: Any = None


@dataclasses.dataclass
class ClassProperties:
  """Properties needed to construct a class."""

  name: str
  fields: list[Field]
  bases: list[Any]

  @classmethod
  def from_field_names(cls, name, field_names, ctx):
    """Make a ClassProperties from field names with no types."""
    fields = [Field(n, ctx.convert.unsolvable, None) for n in field_names]
    return cls(name, fields, [])


def make_annotations_dict(fields, node, ctx):
  locals_ = {
      f.name: abstract_utils.Local(node, None, f.typ, None, ctx) for f in fields
  }
  return abstract.AnnotationsDict(locals_, ctx).to_variable(node)
