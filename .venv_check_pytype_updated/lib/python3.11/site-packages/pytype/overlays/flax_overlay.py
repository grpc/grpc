"""Support for flax.struct dataclasses."""

# Flax is a high-performance neural network library for JAX
# see //third_party/py/flax
#
# Since flax.struct.dataclass uses dataclass.dataclass internally, we can simply
# reuse the dataclass overlay with some subclassed constructors to change the
# module name.
#
# NOTE: flax.struct.dataclasses set frozen=True, but since we don't support
# frozen anyway we needn't bother about that for now.


from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.overlays import classgen
from pytype.overlays import dataclass_overlay
from pytype.overlays import overlay
from pytype.pytd import pytd


class DataclassOverlay(overlay.Overlay):
  """A custom overlay for the 'flax.struct' module."""

  def __init__(self, ctx):
    member_map = {
        "dataclass": Dataclass.make,
    }
    ast = ctx.loader.import_name("flax.struct")
    super().__init__(ctx, "flax.struct", member_map, ast)


class Dataclass(dataclass_overlay.Dataclass):
  """Implements the @dataclass decorator."""

  def decorate(self, node, cls):
    super().decorate(node, cls)
    if not isinstance(cls, abstract.InterpreterClass):
      return
    cls.members["replace"] = classgen.make_replace_method(self.ctx, node, cls)


# NOTE: flax.linen.module.Module is reexported as flax.linen.Module in
# flax.linen/__init__.py. Due to the way the import system interacts with
# overlays, we cannot just provide an overlay for flax.linen.module.Module and
# trust `flax.linen.Module` to redirect to it whenever needed; we have to
# explicitly handle both ways of referring to the class.


class LinenOverlay(overlay.Overlay):
  """A custom overlay for the 'flax.linen' module."""

  def __init__(self, ctx):
    member_map = {
        "Module": Module,
    }
    ast = ctx.loader.import_name("flax.linen")
    super().__init__(ctx, "flax.linen", member_map, ast)


class LinenModuleOverlay(overlay.Overlay):
  """A custom overlay for the 'flax.linen.module' module."""

  def __init__(self, ctx):
    member_map = {
        "Module": Module,
    }
    ast = ctx.loader.import_name("flax.linen.module")
    super().__init__(ctx, "flax.linen.module", member_map, ast)


class ModuleDataclass(dataclass_overlay.Dataclass):
  """Dataclass with automatic 'name' and 'parent' members."""

  def _add_implicit_field(self, node, cls_locals, key, typ):
    if key in cls_locals:
      self.ctx.errorlog.invalid_annotation(
          self.ctx.vm.frames,
          None,
          name=key,
          details=f"flax.linen.Module defines field '{key}' implicitly",
      )
    default = typ.to_variable(node)
    cls_locals[key] = abstract_utils.Local(node, None, typ, default, self.ctx)

  def get_class_locals(self, node, cls):
    cls_locals = super().get_class_locals(node, cls)
    initvar = self.ctx.convert.lookup_value("dataclasses", "InitVar")

    def make_initvar(t):
      return abstract.ParameterizedClass(
          initvar, {abstract_utils.T: t}, self.ctx
      )

    name_type = make_initvar(self.ctx.convert.str_type)
    # TODO(mdemello): Fill in the parent type properly
    parent_type = make_initvar(self.ctx.convert.unsolvable)
    self._add_implicit_field(node, cls_locals, "name", name_type)
    self._add_implicit_field(node, cls_locals, "parent", parent_type)
    return cls_locals

  def decorate(self, node, cls):
    super().decorate(node, cls)
    if not isinstance(cls, abstract.InterpreterClass):
      return
    cls.members["replace"] = classgen.make_replace_method(self.ctx, node, cls)


class Module(abstract.PyTDClass):
  """Construct a dataclass for any class inheriting from Module."""

  IMPLICIT_FIELDS = ("name", "parent")

  # 'Module' can also be imported through an alias in flax.linen, but we always
  # want to use its full, unaliased name.
  _MODULE = "flax.linen.module"

  def __init__(self, ctx, module):
    del module  # unused
    pytd_cls = ctx.loader.lookup_pytd(self._MODULE, "Module")
    # flax.linen.Module loads as a LateType, we need to convert it and then get
    # the pytd.Class back out to use in our own constructor.
    if isinstance(pytd_cls, pytd.Constant):
      pytd_cls = ctx.convert.constant_to_value(pytd_cls).pytd_cls
    super().__init__("Module", pytd_cls, ctx)

  def init_subclass(self, node, cls):
    # Subclasses of Module call self.setup() when creating instances.
    cls.additional_init_methods.append("setup")
    dc = ModuleDataclass.make(self.ctx)
    cls_var = cls.to_variable(node)
    args = function.Args(posargs=(cls_var,), namedargs={})
    node, _ = dc.call(node, None, args)
    return node

  def to_pytd_type_of_instance(
      self, node=None, instance=None, seen=None, view=None
  ):
    """Get the type an instance of us would have."""
    # The class is imported as flax.linen.Module but aliases
    # flax.linen.module.Module internally
    return pytd.NamedType(self.full_name)

  @property
  def full_name(self):
    # Override the full name here rather than overriding the module name in the
    # overlay because we might want to overlay other things from flax.linen.
    return f"{self._MODULE}.{self.name}"

  def __repr__(self):
    return f"Overlay({self.full_name})"
