"""Function definitions in pyi files."""

from collections.abc import Iterable
import dataclasses
from typing import Any

from pytype.pytd import pytd


class OverloadedDecoratorError(Exception):
  """Inconsistent decorators on an overloaded function."""

  def __init__(self, name, typ):
    msg = f"Overloaded signatures for '{name}' disagree on {typ} decorators"
    super().__init__(msg)


class PropertyDecoratorError(Exception):
  """Inconsistent property decorators on an overloaded function."""

  def __init__(self, name, explanation):
    msg = f"Invalid property decorators for '{name}': {explanation}"
    super().__init__(msg)


@dataclasses.dataclass
class Param:
  """Internal representation of function parameters."""

  name: str
  type: pytd.Type | None = None
  default: Any = None
  kind: pytd.ParameterKind = pytd.ParameterKind.REGULAR

  def to_pytd(self) -> pytd.Parameter:
    """Return a pytd.Parameter object for a normal argument."""
    if self.default is not None:
      default_type = self.default
      if self.type is None and default_type != pytd.NamedType("NoneType"):
        self.type = default_type
    if self.type is None:
      self.type = pytd.AnythingType()

    optional = self.default is not None
    return pytd.Parameter(self.name, self.type, self.kind, optional, None)


@dataclasses.dataclass(frozen=True)
class NameAndSig:
  """Internal representation of function signatures."""

  name: str
  signature: pytd.Signature
  decorators: tuple[pytd.Alias, ...] = ()
  is_abstract: bool = False
  is_coroutine: bool = False
  is_final: bool = False
  is_overload: bool = False

  @classmethod
  def make(
      cls, name: str, args: list[tuple[str, pytd.Type]], return_type: pytd.Type
  ) -> "NameAndSig":
    """Make a new NameAndSig from an argument list."""
    params = tuple(Param(n, t).to_pytd() for (n, t) in args)
    sig = pytd.Signature(params=params, return_type=return_type,
                         starargs=None, starstarargs=None,
                         exceptions=(), template=())
    return cls(name, sig)


def pytd_return_type(
    name: str, return_type: pytd.Type | None, is_async: bool
) -> pytd.Type:
  """Convert function return type to pytd."""
  if name == "__init__":
    if (return_type is None or
        isinstance(return_type, pytd.AnythingType)):
      ret = pytd.NamedType("NoneType")
    else:
      ret = return_type
  elif is_async:
    base = pytd.NamedType("typing.Coroutine")
    params = (pytd.AnythingType(), pytd.AnythingType(), return_type)
    ret = pytd.GenericType(base, params)
  elif return_type is None:
    ret = pytd.AnythingType()
  else:
    ret = return_type
  return ret


def pytd_default_star_param() -> pytd.Parameter:
  return pytd.Parameter(
      "args", pytd.NamedType("tuple"), pytd.ParameterKind.REGULAR, True, None)


def pytd_default_starstar_param() -> pytd.Parameter:
  return pytd.Parameter(
      "kwargs", pytd.NamedType("dict"), pytd.ParameterKind.REGULAR, True, None)


def pytd_star_param(name: str, annotation: pytd.Type) -> pytd.Parameter:
  """Return a pytd.Parameter for a *args argument."""
  if annotation is None:
    param_type = pytd.NamedType("tuple")
  else:
    param_type = pytd.GenericType(
        pytd.NamedType("tuple"), (annotation,))
  return pytd.Parameter(
      name, param_type, pytd.ParameterKind.REGULAR, True, None)


def pytd_starstar_param(
    name: str, annotation: pytd.Type
) -> pytd.Parameter:
  """Return a pytd.Parameter for a **kwargs argument."""
  if annotation is None:
    param_type = pytd.NamedType("dict")
  else:
    param_type = pytd.GenericType(
        pytd.NamedType("dict"), (pytd.NamedType("str"), annotation))
  return pytd.Parameter(
      name, param_type, pytd.ParameterKind.REGULAR, True, None)


def _make_param(attr: pytd.Constant, kw_only: bool = False) -> pytd.Parameter:
  p = Param(name=attr.name, type=attr.type, default=attr.value)
  if kw_only:
    p.kind = pytd.ParameterKind.KWONLY
  return p.to_pytd()


def generate_init(
    fields: Iterable[pytd.Constant],
    kw_fields: Iterable[pytd.Constant]
) -> pytd.Function:
  """Build an __init__ method from pytd class constants."""
  self_arg = Param("self").to_pytd()
  params = (self_arg,) + tuple(_make_param(c) for c in fields)
  params += tuple(_make_param(c, True) for c in kw_fields)
  # We call this at 'runtime' rather than from the parser, so we need to use the
  # resolved type of None, rather than NamedType("NoneType")
  ret = pytd.ClassType("builtins.NoneType")
  sig = pytd.Signature(params=params, return_type=ret,
                       starargs=None, starstarargs=None,
                       exceptions=(), template=())
  return pytd.Function("__init__", (sig,), kind=pytd.MethodKind.METHOD)


# -------------------------------------------
# Method signature merging


@dataclasses.dataclass
class _Property:
  type: str
  arity: int


def _property_decorators(name: str) -> dict[str, _Property]:
  """Generates the property decorators for a method name."""
  return {
      "property": _Property("getter", 1),
      (name + ".setter"): _Property("setter", 2),
      (name + ".deleter"): _Property("deleter", 1)
  }


@dataclasses.dataclass
class _Properties:
  """Function property decorators."""

  getter: pytd.Signature | None = None
  setter: pytd.Signature | None = None
  deleter: pytd.Signature | None = None

  def set(self, prop, sig, name):
    assert hasattr(self, prop), prop
    if getattr(self, prop):
      msg = (f"need at most one each of @property, @{name}.setter, and "
             f"@{name}.deleter")
      raise PropertyDecoratorError(name, msg)
    setattr(self, prop, sig)


def _has_decorator(fn, dec):
  return any(d.type.name == dec for d in fn.decorators)


@dataclasses.dataclass
class _DecoratedFunction:
  """A mutable builder for pytd.Function values."""

  name: str
  sigs: list[pytd.Signature]
  is_abstract: bool = False
  is_coroutine: bool = False
  is_final: bool = False
  decorators: tuple[pytd.Alias, ...] = ()
  properties: _Properties | None = dataclasses.field(init=False)
  prop_names: dict[str, _Property] = dataclasses.field(init=False)

  @classmethod
  def make(cls, fn: NameAndSig):
    return cls(
        name=fn.name,
        sigs=[fn.signature],
        is_abstract=fn.is_abstract,
        is_coroutine=fn.is_coroutine,
        is_final=fn.is_final,
        decorators=fn.decorators)

  def __post_init__(self):
    self.prop_names = _property_decorators(self.name)
    prop_decorators = [d for d in self.decorators if d.name in self.prop_names]
    if prop_decorators:
      self.properties = _Properties()
      self.add_property(prop_decorators, self.sigs[0])
    else:
      self.properties = None

  def add_property(self, decorators, sig):
    """Add a property overload."""
    assert decorators
    if len(decorators) > 1:
      msg = "conflicting decorators " + ", ".join(d.name for d in decorators)
      raise PropertyDecoratorError(self.name, msg)
    decorator = decorators[0].name
    prop = self.prop_names[decorator]
    min_params = max_params = 0
    for param in sig.params:
      min_params += int(not param.optional)
      max_params += 1
    if min_params <= prop.arity <= max_params:
      assert self.properties is not None
      self.properties.set(prop.type, sig, self.name)
    else:
      raise TypeError(
          f"Function '{self.name}' decorated by property decorator"
          f" @{decorator} must have {prop.arity} param(s), but actually has"
          f" {len(sig.params)}"
      )

  def add_overload(self, fn: NameAndSig) -> None:
    """Add an overloaded signature to a function."""
    if self.properties:
      prop_decorators = [d for d in fn.decorators if d.name in self.prop_names]
      if not prop_decorators:
        raise OverloadedDecoratorError(self.name, "property")
      self.add_property(prop_decorators, fn.signature)
    else:
      self.sigs.append(fn.signature)
    self._check_overload_consistency(fn)

  def _check_overload_consistency(self, fn: NameAndSig) -> None:
    """Check if the new overload is consistent with existing."""
    # Some decorators need to be consistent for all overloads.
    if self.is_coroutine != fn.is_coroutine:
      raise OverloadedDecoratorError(self.name, "coroutine")
    if self.is_final != fn.is_final:
      raise OverloadedDecoratorError(self.name, "final")
    if (_has_decorator(self, "staticmethod") !=
        _has_decorator(fn, "staticmethod")):
      raise OverloadedDecoratorError(self.name, "staticmethod")
    if _has_decorator(self, "classmethod") != _has_decorator(fn, "classmethod"):
      raise OverloadedDecoratorError(self.name, "classmethod")
    # It's okay for some property overloads to be abstract and others not.
    if not self.properties and self.is_abstract != fn.is_abstract:
      raise OverloadedDecoratorError(self.name, "abstractmethod")


def merge_method_signatures(
    name_and_sigs: list[NameAndSig],
) -> list[pytd.Function]:
  """Group the signatures by name, turning each group into a function."""
  functions = {}
  for fn in name_and_sigs:
    if fn.name not in functions:
      functions[fn.name] = _DecoratedFunction.make(fn)
    else:
      functions[fn.name].add_overload(fn)
  methods = []
  for name, fn in functions.items():
    decorators = []
    is_staticmethod = is_classmethod = False
    for decorator in fn.decorators:
      if decorator.type.name == "staticmethod":
        is_staticmethod = True
      elif decorator.type.name == "classmethod":
        is_classmethod = True
      else:
        decorators.append(decorator)
    if name == "__new__" or is_staticmethod:
      kind = pytd.MethodKind.STATICMETHOD
    elif name == "__init_subclass__" or is_classmethod:
      kind = pytd.MethodKind.CLASSMETHOD
    elif fn.properties:
      kind = pytd.MethodKind.PROPERTY
      # If we have only setters and/or deleters, replace them with a single
      # method foo(...) -> Any, so that we infer a constant `foo: Any` even if
      # the original method signatures are all `foo(...) -> None`. (If we have a
      # getter we use its return type, but in the absence of a getter we want to
      # fall back on Any since we cannot say anything about what the setter sets
      # the type of foo to.)
      if fn.properties.getter:
        fn.sigs = [fn.properties.getter]
      else:
        sig = fn.properties.setter or fn.properties.deleter
        assert sig is not None
        fn.sigs = [sig.Replace(return_type=pytd.AnythingType())]
    else:
      # Other decorators do not affect the kind
      kind = pytd.MethodKind.METHOD
    flags = pytd.MethodFlag.NONE
    if fn.is_abstract:
      flags |= pytd.MethodFlag.ABSTRACT
    if fn.is_coroutine:
      flags |= pytd.MethodFlag.COROUTINE
    if fn.is_final:
      flags |= pytd.MethodFlag.FINAL
    methods.append(pytd.Function(name, tuple(fn.sigs), kind, flags,
                                 tuple(decorators)))
  return methods
