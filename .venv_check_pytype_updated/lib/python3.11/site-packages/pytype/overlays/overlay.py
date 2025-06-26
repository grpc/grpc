"""Base class for module overlays."""

from collections.abc import Callable
from typing import Any

from pytype import datatypes
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.typegraph import cfg

# The first argument type is pytype.context.Context, but we can't import context
# here due to a circular dependency.
BuilderType = Callable[[Any, str], abstract.BaseValue]


class Overlay(abstract.Module):
  """A layer between pytype and a module's pytd definition.

  An overlay pretends to be a module, but provides members that generate extra
  typing information that cannot be expressed in a pytd file. For example,
  collections.namedtuple is a factory method that generates class definitions
  at runtime. An overlay is needed for Pytype to generate these classes.

  An Overlay will typically import its underlying module in its __init__, e.g.
  by calling ctx.loader.import_name(). Due to this, Overlays should only be used
  when their underlying module is imported by the Python script being analyzed!
  A subclass of Overlay should have an __init__ with the signature:
    def __init__(self, ctx)

  Attributes:
    real_module: An abstract.Module wrapping the AST for the underlying module.
  """

  def __init__(self, ctx, name, member_map, ast):
    """Initialize the overlay.

    Args:
      ctx: Instance of context.Context.
      name: A string containing the name of the underlying module.
      member_map: Dict of str to abstract.BaseValues that provide type
        information not available in the underlying module.
      ast: An pytd.TypeDeclUnit containing the AST for the underlying module.
        Used to access type information for members of the module that are not
        explicitly provided by the overlay.
    """
    super().__init__(ctx, name, member_map, ast)
    self.real_module = ctx.convert.constant_to_value(
        ast, subst=datatypes.AliasingDict(), node=ctx.root_node
    )

  def _convert_member(
      self,
      name: str,
      member: BuilderType,
      subst: dict[str, cfg.Variable] | None = None,
  ) -> cfg.Variable:
    val = member(self.ctx, self.name)
    val.module = self.name
    return val.to_variable(self.ctx.root_node)

  def get_module(self, name):
    """Returns the abstract.Module for the given name."""
    if name in self._member_map:
      return self
    else:
      return self.real_module

  def items(self):
    items = super().items()
    items += [
        (name, item)
        for name, item in self.real_module.items()
        if name not in self._member_map
    ]
    return items

  def maybe_load_member(self, member_name):
    """Try to lazily load a member by name."""
    # We may encounter errors such as [recursion-error] from recursive loading
    # of a TypingContainer or [not-supported-yet] for a typing feature in a
    # too-low version. If there are errors, we discard the result.
    with self.ctx.errorlog.checkpoint() as record:
      member_var = self.load_lazy_attribute(member_name, store=False)
    member = abstract_utils.get_atomic_value(member_var)
    # AnnotationClass is a placeholder used in the construction of parameterized
    # types, not a real type.
    if record.errors or isinstance(member, abstract.AnnotationClass):
      return None
    self.members[member_name] = member_var
    return member


def add_name(name, builder):
  """Turns (name, ctx, module) -> val signatures into (ctx, module) -> val."""
  return lambda ctx, module: builder(name, ctx, module)


def drop_module(builder):
  """Turns (ctx) -> val signatures into (ctx, module) -> val."""
  return lambda ctx, module: builder(ctx)
