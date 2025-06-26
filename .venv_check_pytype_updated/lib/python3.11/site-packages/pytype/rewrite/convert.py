"""Conversion from pytd to abstract representations of Python values."""


from pytype.pytd import pytd
from pytype.rewrite.abstract import abstract
from pytype.rewrite.overlays import overlays


class _Cache:

  def __init__(self):
    self.classes = {}
    self.funcs = {}
    self.types = {}


class AbstractConverter:
  """Pytd -> abstract converter."""

  def __init__(self, ctx: abstract.ContextType):
    self._ctx = ctx
    self._cache = _Cache()
    overlays.initialize()

  def pytd_class_to_value(self, cls: pytd.Class) -> abstract.SimpleClass:
    """Converts a pytd class to an abstract class."""
    if cls in self._cache.classes:
      return self._cache.classes[cls]
    # TODO(b/324464265): Handle keywords, bases, decorators, slots, template
    module, _, name = cls.name.rpartition('.')
    members = {}
    keywords = {kw: self.pytd_type_to_value(val) for kw, val in cls.keywords}
    abstract_class = abstract.SimpleClass(
        ctx=self._ctx,
        name=name,
        members=members,
        bases=(),
        keywords=keywords,
        module=module or None,
    )
    # Cache the class early so that references to it in its members and bases
    # don't cause infinite recursion.
    self._cache.classes[cls] = abstract_class
    for method in cls.methods:
      # For consistency with InterpreterFunction, prepend the class name.
      full_name = f'{name}.{method.name}'
      method_value = self.pytd_function_to_value(method, (module, full_name))
      abstract_class.members[method.name] = method_value
    for constant in cls.constants:
      constant_type = self.pytd_type_to_value(constant.type)
      abstract_class.members[constant.name] = constant_type.instantiate()
    for nested_class in cls.classes:
      abstract_class.members[nested_class.name] = self.pytd_class_to_value(
          nested_class
      )
    bases = []
    for base in cls.bases:
      if isinstance(base, pytd.GenericType):
        # TODO(b/292160579): Handle generics.
        base = base.base_type
      if isinstance(base, pytd.ClassType):
        base = base.cls
      if isinstance(base, pytd.Class):
        bases.append(self.pytd_class_to_value(base))
      else:
        raise NotImplementedError(f"I can't handle this base class: {base}")
    abstract_class.bases = tuple(bases)
    for base in bases:
      if base.full_name in overlays.CLASS_TRANSFORMS:
        overlays.CLASS_TRANSFORMS[base.full_name](self._ctx, abstract_class)
    return abstract_class

  def pytd_function_to_value(
      self,
      func: pytd.Function,
      func_name: tuple[str, str] | None = None,
  ) -> abstract.PytdFunction:
    """Converts a pytd function to an abstract function."""
    if func in self._cache.funcs:
      return self._cache.funcs[func]
    if func_name:
      module, name = func_name
    else:
      module, _, name = func.name.rpartition('.')
    signatures = tuple(
        abstract.Signature.from_pytd(self._ctx, name, pytd_sig)
        for pytd_sig in func.signatures
    )
    builder = overlays.FUNCTIONS.get((module, name), abstract.PytdFunction)
    abstract_func = builder(
        ctx=self._ctx,
        name=name,
        signatures=signatures,
        module=module or None,
    )
    self._cache.funcs[func] = abstract_func
    return abstract_func

  def pytd_type_to_value(self, typ: pytd.Type) -> abstract.BaseValue:
    """Converts a pytd type to an abstract value.

    Args:
      typ: The type.

    Returns:
      The abstract representation of the type. For example, when passed
      `pytd.ClassType(pytd.Class(int))`, this function returns
      `abstract.SimpleClass(int)`.
    """
    if typ not in self._cache.types:
      self._cache.types[typ] = self._pytd_type_to_value(typ)
    return self._cache.types[typ]

  def _pytd_type_to_value(self, typ: pytd.Type) -> abstract.BaseValue:
    """Helper for pytd_type_to_value."""
    if isinstance(typ, pytd.ClassType):
      return self.pytd_class_to_value(typ.cls)
    elif isinstance(typ, pytd.AnythingType):
      return self._ctx.consts.singles['Any']
    elif isinstance(typ, pytd.NothingType):
      return self._ctx.consts.singles['Never']
    elif isinstance(typ, pytd.UnionType):
      return abstract.Union(
          self._ctx, tuple(self._pytd_type_to_value(t) for t in typ.type_list)
      )
    # TODO(b/324464265): Everything from this point onward is a dummy
    # implementation that needs to be replaced by a real one.
    elif isinstance(typ, pytd.GenericType):
      return self._pytd_type_to_value(typ.base_type)
    elif isinstance(typ, pytd.TypeParameter):
      return self._ctx.consts.Any
    elif isinstance(typ, pytd.Literal):
      return self._ctx.types[type(typ.value)]
    elif isinstance(typ, pytd.Annotated):
      # We discard the Annotated wrapper for now, but we will need to keep track
      # of it because Annotated is a special form that can be used in generic
      # type aliases.
      return self._pytd_type_to_value(typ.base_type)
    elif isinstance(typ, (pytd.LateType, pytd.IntersectionType)):
      raise NotImplementedError(
          f'Abstract conversion not yet implemented for {typ}'
      )
    else:
      raise ValueError(f'Cannot convert {typ} to an abstract value')

  def pytd_alias_to_value(self, alias: pytd.Alias) -> abstract.BaseValue:
    if isinstance(alias.type, pytd.Module):
      return abstract.Module(self._ctx, alias.type.module_name)
    raise NotImplementedError(
        f'Abstract conversion not yet implemented for {alias}'
    )
