"""Utilities for quoting, name-mangling, etc."""

import re


# The partial prefix was originally `~` and some part of the protocol inference
# code relies on it sorting later than `typing.`, so we start the prefix with
# `z` to make sure it comes last in the sort order. To reproduce, remove the `z`
# and test tests/test_protocol_inference
PARTIAL = "z__pytype_partial__"
UNKNOWN = PARTIAL + "unknown"


def pack_partial(name: str) -> str:
  """Pack a name, for unpacking with unpack_partial()."""
  return PARTIAL + name.replace(".", PARTIAL)


def unpack_partial(name: str) -> str:
  """Convert e.g. "~int" to "int"."""
  assert name.startswith(PARTIAL)
  return name[len(PARTIAL) :].replace(PARTIAL, ".")


def is_partial(cls) -> bool:
  """Returns True if this is a partial class, e.g. "~list"."""
  if isinstance(cls, str):
    return cls.startswith(PARTIAL)
  elif hasattr(cls, "name"):
    return cls.name.startswith(PARTIAL)
  else:
    return False


def is_complete(cls) -> bool:
  return not is_partial(cls)


def unknown(idcode: int) -> str:
  return UNKNOWN + str(idcode)


def is_unknown(name: str) -> bool:
  return name.startswith(UNKNOWN)


def preprocess_pytd(text: str) -> str:
  """Replace ~ in a text pytd with PARTIAL."""
  return text.replace("~", PARTIAL)


def pack_namedtuple(name: str, fields: list[str]) -> str:
  """Generate a name for a namedtuple proxy class."""
  return f"namedtuple_{name}_{'_'.join(fields)}"


def _pack_base_class(typename: str, basename: str, index: int) -> str:
  return f"{typename}_{basename}_{index}"


def pack_namedtuple_base_class(name: str, index: int) -> str:
  """Generate a name for a namedtuple proxy base class."""
  return _pack_base_class("namedtuple", name, index)


def unpack_namedtuple(name: str) -> str:
  """Retrieve the original namedtuple class name."""
  return re.sub(r"\bnamedtuple[-_]([^-_]+)[-_\w]*", r"\1", name)


def pack_newtype_base_class(name: str, index: int) -> str:
  """Generate a name for a NewType proxy base class."""
  return _pack_base_class("newtype", name, index)


def pack_typeddict_base_class(name: str, index: int) -> str:
  """Generate a name for a TypedDict proxy base class."""
  return _pack_base_class("typeddict", name, index)
