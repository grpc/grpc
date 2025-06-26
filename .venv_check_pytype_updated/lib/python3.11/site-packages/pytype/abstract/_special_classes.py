"""Classes that need special handling, typically due to code generation."""

from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.pytd import pytd


def build_class(node, props, kwargs, ctx):
  """Handle classes whose subclasses define their own class constructors."""

  for base in props.bases:
    base = abstract_utils.get_atomic_value(base, default=None)
    if not isinstance(base, class_mixin.Class):
      continue
    if base.is_enum:
      enum_base = abstract_utils.get_atomic_value(
          ctx.vm.loaded_overlays["enum"].members["Enum"]
      )
      return enum_base.make_class(node, props)
    elif base.full_name == "typing.NamedTuple":
      return base.make_class(node, props.bases, props.class_dict_var)
    elif base.is_typed_dict_class:
      return base.make_class(
          node, props.bases, props.class_dict_var, total=kwargs.get("total")
      )
    elif "__dataclass_transform__" in base.metadata:
      node, cls_var = ctx.make_class(node, props)
      return ctx.convert.apply_dataclass_transform(cls_var, node)
  return node, None


class _Builder:
  """Build special classes created by inheriting from a specific class."""

  def __init__(self, ctx):
    self.ctx = ctx
    self.convert = ctx.convert

  def matches_class(self, c):
    raise NotImplementedError()

  def matches_base(self, c):
    raise NotImplementedError()

  def matches_mro(self, c):
    raise NotImplementedError()

  def make_base_class(self):
    raise NotImplementedError()

  def make_derived_class(self, name, pytd_cls):
    raise NotImplementedError()

  def maybe_build_from_pytd(self, name, pytd_cls):
    if self.matches_class(pytd_cls):
      return self.make_base_class()
    elif self.matches_base(pytd_cls):
      return self.make_derived_class(name, pytd_cls)
    else:
      return None

  def maybe_build_from_mro(self, abstract_cls, name, pytd_cls):
    if self.matches_mro(abstract_cls):
      return self.make_derived_class(name, pytd_cls)
    return None


class _TypedDictBuilder(_Builder):
  """Build a typed dict."""

  CLASSES = ("typing.TypedDict", "typing_extensions.TypedDict")

  def matches_class(self, c):
    return c.name in self.CLASSES

  def matches_base(self, c):
    return any(
        isinstance(b, pytd.ClassType) and self.matches_class(b) for b in c.bases
    )

  def matches_mro(self, c):
    # Check if we have typed dicts in the MRO by seeing if we have already
    # created a TypedDictClass for one of the ancestor classes.
    return any(
        isinstance(b, class_mixin.Class) and b.is_typed_dict_class
        for b in c.mro
    )

  def make_base_class(self):
    return self.convert.make_typed_dict_builder()

  def make_derived_class(self, name, pytd_cls):
    return self.convert.make_typed_dict(name, pytd_cls)


class _NamedTupleBuilder(_Builder):
  """Build a namedtuple."""

  CLASSES = ("typing.NamedTuple",)

  def matches_class(self, c):
    return c.name in self.CLASSES

  def matches_base(self, c):
    return any(
        isinstance(b, pytd.ClassType) and self.matches_class(b) for b in c.bases
    )

  def matches_mro(self, c):
    # We only create namedtuples by direct inheritance
    return False

  def make_base_class(self):
    return self.convert.make_namedtuple_builder()

  def make_derived_class(self, name, pytd_cls):
    return self.convert.make_namedtuple(name, pytd_cls)


_BUILDERS = (_TypedDictBuilder, _NamedTupleBuilder)


def maybe_build_from_pytd(name, pytd_cls, ctx):
  """Try to build a special class from a pytd class."""
  for b in _BUILDERS:
    ret = b(ctx).maybe_build_from_pytd(name, pytd_cls)
    if ret:
      return ret
  return None


def maybe_build_from_mro(abstract_cls, name, pytd_cls, ctx):
  """Try to build a special class from the MRO of an abstract class."""
  for b in _BUILDERS:
    ret = b(ctx).maybe_build_from_mro(abstract_cls, name, pytd_cls)
    if ret:
      return ret
  return None
