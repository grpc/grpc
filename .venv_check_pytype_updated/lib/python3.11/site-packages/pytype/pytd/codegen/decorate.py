"""Apply decorators to classes and functions."""

from collections.abc import Iterable

from pytype.pytd import base_visitor
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd.codegen import function


class ValidateDecoratedClassVisitor(base_visitor.Visitor):
  """Apply class decorators."""

  def EnterClass(self, cls):
    validate_class(cls)


def _decorate_class(cls: pytd.Class, decorator: str) -> pytd.Class:
  """Apply a single decorator to a class."""
  factory = _DECORATORS.get(decorator, None)
  if factory:
    return factory(cls)
  else:
    # do nothing for unknown decorators
    return cls


def _validate_class(cls: pytd.Class, decorator: str) -> None:
  """Validate a single decorator for a class."""
  validator = _VALIDATORS.get(decorator, None)
  if validator:
    validator(cls)


def _decorator_names(cls: pytd.Class) -> list[str]:
  return [
      x.type.name for x in reversed(cls.decorators) if x.type.name is not None
  ]


def has_decorator(cls: pytd.Class, names) -> bool:
  return bool(set(names) & set(_decorator_names(cls)))


def check_defaults(fields: Iterable[pytd.Constant], cls_name: str):
  """Check that a non-default field does not follow a default one."""
  default = None
  for c in fields:
    if c.value is not None:
      default = c.name
    elif default:
      raise TypeError(
          f"In class {cls_name}: "
          f"non-default argument {c.name} follows default argument {default}")


def check_class(cls: pytd.Class) -> None:
  if not has_init(cls):
    # Check that we would generate a valid __init__ (without non-defaults
    # following defaults)
    fields = get_attributes(cls)
    check_defaults(fields, cls.name)


def is_dataclass_kwonly(c: pytd.Type) -> bool:
  return (
      (isinstance(c, pytd.NamedType) and
       c.name == "dataclasses.KW_ONLY") or
      (isinstance(c, pytd.ClassType) and
       c.cls and c.cls.name == "dataclasses.KW_ONLY"))


def add_init_from_fields(
    cls: pytd.Class,
    fields: Iterable[pytd.Constant]
) -> pytd.Class:
  """Add a generated __init__ function based on class constants."""
  pos = []
  kw = []
  kw_only = False
  for f in fields:
    if is_dataclass_kwonly(f.type):
      kw_only = True
    elif kw_only:
      kw.append(f)
    else:
      pos.append(f)
  check_defaults(pos, cls.name)
  init = function.generate_init(pos, kw)
  methods = cls.methods + (init,)
  return cls.Replace(methods=methods)


def get_attributes(cls: pytd.Class):
  """Get class attributes, filtering out properties and ClassVars."""
  attributes = []
  for c in cls.constants:
    if isinstance(c.type, pytd.Annotated):
      if "'property'" not in c.type.annotations:
        c = c.Replace(type=c.type.base_type)
        attributes.append(c)
    elif c.name == "__doc__":
      # Filter docstrings out from the attribs list
      # (we emit them as `__doc__ : str` in pyi output)
      pass
    elif c.type.name == "typing.ClassVar":
      # We do not want classvars treated as attribs
      pass
    else:
      attributes.append(c)
  return tuple(attributes)


def has_init(cls: pytd.Class) -> bool:
  """Check if the class has an explicit __init__ method."""
  return any(x.name == "__init__" for x in cls.methods)


def add_generated_init(cls: pytd.Class) -> pytd.Class:
  # Do not override an __init__ from the pyi file
  if has_init(cls):
    return cls
  fields = get_attributes(cls)
  return add_init_from_fields(cls, fields)


def get_field_type_union(cls: pytd.Class):
  fields = get_attributes(cls)
  return pytd_utils.JoinTypes(x.type for x in fields)


def add_attrs_attrs(cls: pytd.Class) -> pytd.Class:
  if "__attrs_attrs__" in (x.name for x in cls.constants):
    return cls
  types = get_field_type_union(cls)
  params = pytd.GenericType(pytd.LateType("attr.Attribute"), (types,))
  aa = pytd.GenericType(pytd.LateType("builtins.tuple"), (params,))
  attrs_attrs = pytd.Constant("__attrs_attrs__", aa)
  constants = cls.constants + (attrs_attrs,)
  return cls.Replace(constants=constants)


def decorate_attrs(cls: pytd.Class) -> pytd.Class:
  cls = add_generated_init(cls)
  return add_attrs_attrs(cls)


def add_dataclass_fields(cls: pytd.Class) -> pytd.Class:
  if "__dataclass_fields__" in (x.name for x in cls.constants):
    return cls
  types = get_field_type_union(cls)
  k = pytd.LateType("builtins.str")
  v = pytd.GenericType(pytd.LateType("dataclasses.Field"), (types,))
  df = pytd.GenericType(pytd.LateType("builtins.dict"), (k, v))
  dataclass_fields = pytd.Constant("__dataclass_fields__", df)
  constants = cls.constants + (dataclass_fields,)
  return cls.Replace(constants=constants)


def decorate_dataclass(cls: pytd.Class) -> pytd.Class:
  cls = add_generated_init(cls)
  return add_dataclass_fields(cls)


def process_class(cls: pytd.Class) -> pytd.Class:
  """Apply all decorators to a class."""
  for decorator in _decorator_names(cls):
    cls = _decorate_class(cls, decorator)
  return cls


def validate_class(cls: pytd.Class) -> None:
  for decorator in _decorator_names(cls):
    _validate_class(cls, decorator)


# NOTE: For attrs, the resolved "real name" of the decorator in pyi files is
# attr._make.attrs; the aliases are added here in case the attrs stub files
# change to hide that implementation detail. We also add an implicit
# "auto_attribs=True" to @attr.s decorators in pyi files.

_DECORATORS = {
    "dataclasses.dataclass": decorate_dataclass,
    "attr.s": decorate_attrs,
    "attr.attrs": decorate_attrs,
    "attr._make.attrs": decorate_attrs,
    "attr.define": decorate_attrs,
}


_VALIDATORS = {
    "dataclasses.dataclass": check_class,
    "attr.s": check_class,
    "attr.attrs": check_class,
    "attr._make.attrs": check_class,
    "attr.define": check_class,
}
