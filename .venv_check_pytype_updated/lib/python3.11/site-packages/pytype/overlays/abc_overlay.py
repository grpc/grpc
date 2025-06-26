"""Implementation of special members of Python's abc library."""

from pytype.abstract import abstract
from pytype.overlays import overlay
from pytype.overlays import special_builtins


def _set_abstract(args, argname):
  if args.posargs:
    func_var = args.posargs[0]
  else:
    func_var = args.namedargs[argname]
  for func in func_var.data:
    if isinstance(func, abstract.FUNCTION_TYPES):
      func.is_abstract = True
  return func_var


class ABCOverlay(overlay.Overlay):
  """A custom overlay for the 'abc' module."""

  def __init__(self, ctx):
    member_map = {
        "abstractclassmethod": AbstractClassMethod.make,
        "abstractmethod": AbstractMethod.make,
        "abstractproperty": AbstractProperty.make,
        "abstractstaticmethod": AbstractStaticMethod.make,
        "ABCMeta": overlay.add_name(
            "ABCMeta", special_builtins.Type.make_alias
        ),
    }
    ast = ctx.loader.import_name("abc")
    super().__init__(ctx, "abc", member_map, ast)


class AbstractClassMethod(special_builtins.ClassMethod):
  """Implements abc.abstractclassmethod."""

  @classmethod
  def make(cls, ctx, module):
    return super().make_alias("abstractclassmethod", ctx, module)

  def call(self, node, func, args, alias_map=None):
    _ = _set_abstract(args, "callable")
    return super().call(node, func, args, alias_map)


class AbstractMethod(abstract.PyTDFunction):
  """Implements the @abc.abstractmethod decorator."""

  @classmethod
  def make(cls, ctx, module):
    return super().make("abstractmethod", ctx, module)

  def call(self, node, func, args, alias_map=None):
    """Marks that the given function is abstract."""
    del func, alias_map  # unused
    self.match_args(node, args)
    return node, _set_abstract(args, "funcobj")


class AbstractProperty(special_builtins.Property):
  """Implements the @abc.abstractproperty decorator."""

  @classmethod
  def make(cls, ctx, module):
    return super().make_alias("abstractproperty", ctx, module)

  def call(self, node, func, args, alias_map=None):
    property_args = self._get_args(args)
    for v in property_args.values():
      for b in v.bindings:
        f = b.data
        # If this check fails, we will raise a 'property object is not callable'
        # error down the line.
        # TODO(mdemello): This is in line with what python does, but we could
        # have a more precise error message that insisted f was a class method.
        if isinstance(f, abstract.Function):
          f.is_abstract = True
    return node, special_builtins.PropertyInstance(
        self.ctx, self.name, self, **property_args
    ).to_variable(node)


class AbstractStaticMethod(special_builtins.StaticMethod):
  """Implements abc.abstractstaticmethod."""

  @classmethod
  def make(cls, ctx, module):
    return super().make_alias("abstractstaticmethod", ctx, module)

  def call(self, node, func, args, alias_map=None):
    _ = _set_abstract(args, "callable")
    return super().call(node, func, args, alias_map)
