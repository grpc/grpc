"""Implementation of types from the fiddle library."""

import re
from typing import Any

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.overlays import classgen
from pytype.overlays import overlay
from pytype.pytd import pytd


# Type aliases so we aren't importing stuff purely for annotations
Node = Any
Variable = Any


# Cache instances, so that we don't generate two different classes when
# Config[Foo] is used in two separate places. We use a tuple of the abstract
# class of Foo and a string (either "Config" or "Partial") as a key and store
# the generated Buildable instance (either Config or Partial) as a value.
_INSTANCE_CACHE: dict[tuple[Node, abstract.Class, str], abstract.Instance] = {}


_CLASS_ALIASES = {
    "Config": "Config",
    "PaxConfig": "Config",
    "Partial": "Partial",
    "PaxPartial": "Partial",
}


class FiddleOverlay(overlay.Overlay):
  """A custom overlay for the 'fiddle' module."""

  def __init__(self, ctx):
    """Initializes the FiddleOverlay.

    This function loads the AST for the fiddle module, which is used to
    access type information for any members that are not explicitly provided by
    the overlay. See get_attribute in attribute.py for how it's used.

    Args:
      ctx: An instance of context.Context.
    """
    if ctx.options.use_fiddle_overlay:
      member_map = {
          "Config": overlay.add_name("Config", BuildableBuilder),
          "Partial": overlay.add_name("Partial", BuildableBuilder),
      }
    else:
      member_map = {}

    ast = ctx.loader.import_name("fiddle")
    super().__init__(ctx, "fiddle", member_map, ast)


class BuildableBuilder(abstract.PyTDClass, mixin.HasSlots):
  """Factory for creating fiddle.Config classes."""

  def __init__(self, name, ctx, module):
    pytd_cls = ctx.loader.lookup_pytd(module, name)
    # fiddle.Config/Partial loads as a LateType, convert to pytd.Class
    if isinstance(pytd_cls, pytd.Constant):
      pytd_cls = ctx.convert.constant_to_value(pytd_cls).pytd_cls
    super().__init__(name, pytd_cls, ctx)
    mixin.HasSlots.init_mixin(self)
    self.set_native_slot("__getitem__", self.getitem_slot)
    # For consistency with the rest of the overlay
    self.fiddle_type_name = _CLASS_ALIASES[name]
    self.module = module

  def __repr__(self):
    return f"FiddleBuildableBuilder[{self.name}]"

  def _match_pytd_init(self, node, init_var, args):
    init = init_var.data[0]
    old_pytd_sigs = []
    for signature in init.signatures:
      old_pytd_sig = signature.pytd_sig
      signature.pytd_sig = old_pytd_sig.Replace(
          params=tuple(p.Replace(optional=True) for p in old_pytd_sig.params)
      )
      old_pytd_sigs.append(old_pytd_sig)
    try:
      init.match_args(node, args)
    finally:
      for signature, old_pytd_sig in zip(init.signatures, old_pytd_sigs):
        signature.pytd_sig = old_pytd_sig

  def _match_interpreter_init(self, node, init_var, args):
    # Buildables support partial initialization, so give every parameter a
    # default when matching __init__.
    init = init_var.data[0]
    old_defaults = {}
    for k in init.signature.param_names:
      old_defaults[k] = init.signature.defaults.get(k)
      init.signature.defaults[k] = self.ctx.new_unsolvable(node)
    # TODO(mdemello): We are calling the function and discarding the return
    # value, when ideally we should just call function.match_all_args().
    try:
      function.call_function(self.ctx, node, init_var, args)
    finally:
      for k, default in old_defaults.items():
        if default:
          init.signature.defaults[k] = default
        else:
          del init.signature.defaults[k]

  def _make_init_args(self, node, underlying, args, kwargs):
    """Unwrap Config instances for arg matching."""

    def unwrap(arg_var):
      # If an arg has a Config object, just use its underlying type and don't
      # bother with the rest of the bindings (assume strict arg matching)
      for d in arg_var.data:
        if isinstance(d, Buildable):
          if isinstance(d.underlying, abstract.FUNCTION_TYPES):
            # If the underlying type is a function, do not try to instantiate it
            return self.ctx.new_unsolvable(node)
          else:
            # Match either Config[A] or A
            # TODO(mdemello): This is to prevent issues when a dataclass field
            # has type Config[A] rather than A, in which case blindly unwrapping
            # an arg of type Config[A] is wrong. We should ideally do arg-by-arg
            # matching here instead of trying to construct function args without
            # reference to the signature we are matching.
            return self.ctx.join_variables(
                node, [arg_var, d.underlying.instantiate(node)]
            )
      return arg_var

    new_args = (underlying.instantiate(node),)
    new_args += tuple(unwrap(arg) for arg in args[1:])
    new_kwargs = {k: unwrap(arg) for k, arg in kwargs.items()}
    return function.Args(posargs=new_args, namedargs=new_kwargs)

  def _check_init_args(self, node, underlying, args, kwargs):
    # Configs can be initialized either with no args, e.g. Config(Class) or with
    # initial values, e.g. Config(Class, x=10, y=20). We need to check here that
    # the extra args match the underlying __init__ signature.
    if len(args) > 1 or kwargs:
      _, init_var = self.ctx.attribute_handler.get_attribute(
          node, underlying, "__init__"
      )
      if abstract_utils.is_dataclass(underlying):
        # Only do init matching for dataclasses for now
        args = self._make_init_args(node, underlying, args, kwargs)
        init = init_var.data[0]
        if isinstance(init, abstract.PyTDFunction):
          self._match_pytd_init(node, init_var, args)
        else:
          self._match_interpreter_init(node, init_var, args)

  def new_slot(
      self, node, unused_cls, *args, **kwargs
  ) -> tuple[Node, abstract.Instance]:
    """Create a Config or Partial instance from args."""

    underlying = args[0].data[0]
    self._check_init_args(node, underlying, args, kwargs)

    # Now create the Config object.
    node, ret = make_instance(self.name, underlying, node, self.ctx)
    return node, ret.to_variable(node)

  def getitem_slot(self, node, index_var) -> tuple[Node, abstract.Instance]:
    """Specialize the generic class with the value of index_var."""

    underlying = index_var.data[0]
    ret = BuildableType.make(
        self.fiddle_type_name, underlying, self.ctx, module=self.module
    )
    return node, ret.to_variable(node)

  def get_own_new(self, node, value) -> tuple[Node, Variable]:
    new = abstract.NativeFunction("__new__", self.new_slot, self.ctx)
    return node, new.to_variable(node)


class BuildableType(abstract.ParameterizedClass):
  """Base generic class for fiddle.Config and fiddle.Partial."""

  def __init__(self, base_cls, underlying, ctx, template=None, module="fiddle"):
    if isinstance(underlying, abstract.FUNCTION_TYPES):
      # We don't support functions for now, but falling back to Any here gets us
      # as much of the functionality as possible.
      formal_type_parameters = {abstract_utils.T: ctx.convert.unsolvable}
    elif isinstance(underlying, abstract.ConcreteValue):
      # We should not hit this case but there are some complex cases where we
      # handle __getitem__ wrong.
      formal_type_parameters = {abstract_utils.T: ctx.convert.unsolvable}
    else:
      # Classes and TypeVars
      formal_type_parameters = {abstract_utils.T: underlying}

    super().__init__(base_cls, formal_type_parameters, ctx, template)  # pytype: disable=wrong-arg-types
    self.fiddle_type_name = base_cls.fiddle_type_name
    self.underlying = underlying
    self.module = module

  @classmethod
  def make(
      cls, fiddle_type_name, underlying, ctx, template=None, module="fiddle"
  ):
    base_cls = BuildableBuilder(fiddle_type_name, ctx, module)
    return cls(base_cls, underlying, ctx, template, module)

  def replace(self, inner_types):
    inner_types = dict(inner_types)
    new_underlying = inner_types[abstract_utils.T]
    typ = self.__class__
    return typ.make(
        self.fiddle_type_name,
        new_underlying,
        self.ctx,
        self.template,
        self.module,
    )

  def instantiate(self, node, container=None):
    _, ret = make_instance(
        self.fiddle_type_name, self.underlying, node, self.ctx
    )
    return ret.to_variable(node)

  def __repr__(self):
    return f"{self.fiddle_type_name}Type[{self.underlying}]"


class Buildable(abstract.Instance, mixin.HasSlots):
  """Base class for Config and Partial instances."""

  def __init__(self, fiddle_type_name, cls, ctx, container=None):
    super().__init__(cls, ctx, container)
    self.fiddle_type_name = fiddle_type_name
    self.underlying = None
    mixin.HasSlots.init_mixin(self)
    self.set_native_slot("__getitem__", self.getitem_slot)

  def getitem_slot(self, node, slice_var) -> tuple[Node, abstract.Instance]:
    # We need to set this here otherwise we walk up the chain and call
    # getitem_slot on BuildableBuilder, which tries to create an
    # AnnotationContainer.
    # TODO(mdemello): This probably needs to delegate to
    # vm_utils._call_binop_on_bindings with the lhs set to self.underlying.
    return node, self.ctx.new_unsolvable(node)


class Config(Buildable):
  """An instantiation of a fiddle.Config with a particular template."""

  def __init__(self, *args, **kwargs):
    super().__init__("Config", *args, **kwargs)


class Partial(Buildable):
  """An instantiation of a fiddle.Partial with a particular template."""

  def __init__(self, *args, **kwargs):
    super().__init__("Partial", *args, **kwargs)


def _convert_type(typ, subst, ctx):
  """Helper function for recursive type conversion of fields."""
  if isinstance(typ, abstract.TypeParameter) and typ.name in subst:
    # TODO(mdemello): Handle typevars in unions.
    typ = subst[typ.name]
  new_typ = BuildableType.make("Config", typ, ctx, module="fiddle")
  return abstract.Union([new_typ, typ], ctx)


def _make_fields(typ, ctx):
  """Helper function for recursive type conversion of fields."""
  if isinstance(typ, abstract.ParameterizedClass):
    subst = typ.formal_type_parameters
    typ = typ.base_cls
  else:
    subst = {}
  if abstract_utils.is_dataclass(typ):
    fields = [
        classgen.Field(x.name, _convert_type(x.typ, subst, ctx), x.default)
        for x in typ.metadata["__dataclass_fields__"]
    ]
    return fields
  return []


def make_instance(
    subclass_name: str, underlying: abstract.Class, node, ctx
) -> tuple[Node, abstract.BaseValue]:
  """Generate a Buildable instance from an underlying template class."""

  subclass_name = _CLASS_ALIASES[subclass_name]
  if subclass_name not in ("Config", "Partial"):
    raise ValueError(f"Unexpected instance class: {subclass_name}")

  # We include the root node in case the cache is shared between multiple runs.
  cache_key = (ctx.root_node, underlying, subclass_name)
  if cache_key in _INSTANCE_CACHE:
    return node, _INSTANCE_CACHE[cache_key]
  _INSTANCE_CACHE[cache_key] = ctx.convert.unsolvable  # recursion handling

  instance_class = {"Config": Config, "Partial": Partial}[subclass_name]
  # Create the specialized class Config[underlying] or Partial[underlying]
  try:
    cls = BuildableType.make(subclass_name, underlying, ctx, module="fiddle")
  except KeyError:
    # We are in the middle of constructing the fiddle ast; fiddle.Config doesn't
    # exist yet
    return node, ctx.convert.unsolvable
  # Now create the instance, setting its class to `cls`
  obj = instance_class(cls, ctx)
  obj.underlying = underlying
  fields = _make_fields(underlying, ctx)
  for f in fields:
    obj.members[f.name] = f.typ.instantiate(node)
  # Add a per-instance annotations dict so setattr can be typechecked.
  obj.members["__annotations__"] = classgen.make_annotations_dict(
      fields, node, ctx
  )
  _INSTANCE_CACHE[cache_key] = obj
  return node, obj


def is_fiddle_buildable_pytd(cls: pytd.Class) -> bool:
  # We need the awkward check for the full name because while fiddle reexports
  # the class as fiddle.Config, we expand that in inferred pyi files to
  # fiddle._src.config.Config
  fiddle = re.fullmatch(r"fiddle\.(.+\.)?(Config|Partial)", cls.name)
  pax = re.fullmatch(r"(.+\.)?pax_fiddle.(Pax)?(Config|Partial)", cls.name)
  return bool(fiddle or pax)


def get_fiddle_buildable_subclass(cls: pytd.Class) -> str:
  if re.search(r"\.(Pax)?Config$", cls.name):
    return "Config"
  if re.search(r"\.(Pax)?Partial$", cls.name):
    return "Partial"
  raise ValueError(
      f"Unexpected {cls.name} when computing fiddle Buildable "
      "subclass; allowed suffixes are `.Config`, and `.Partial`."
  )
