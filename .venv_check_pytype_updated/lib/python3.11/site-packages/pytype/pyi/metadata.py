"""Support for PEP593 (typing.Annotated) metadata.

PEP593 allows metadata to be arbitrary python values. We convert these to
strings to support the fact that pytd nodes are immutable, and provide
converters back to python values.

There are two separate forms of encoding: Internally, we want to deal with
metadata as dicts with string keys, which we convert to strings to store in a
pytd.Annotated node and back to dicts via `metadata.{to,from}_string`. When
writing out to a pyi/pytd file, we pretty-print some types to support a more
human-readable annotation, via `metadata.to_pytd`, and parse those back to dicts
in pyi/parser.py.

Note that only a subset of values is supported, see pyi/parser.py
"""

import dataclasses
from typing import Any

from pytype.pyi import evaluator


@dataclasses.dataclass
class Call:
  """Convert callables in metadata to/from generic dicts."""

  fn: str
  posargs: list[Any]
  kwargs: dict[str, Any]

  @classmethod
  def from_metadata(cls, md, posarg_names, kwarg_names):
    fn = md["tag"]
    posargs = [md[k] for k in posarg_names if k in md]
    kwargs = {k: md[k] for k in kwarg_names if k in md}
    return cls(fn, posargs, kwargs)

  @classmethod
  def from_call_dict(cls, md):
    assert md["tag"] == "call"
    fn = md["fn"]
    posargs = md["posargs"] or []
    kwargs = md["kwargs"] or {}
    return cls(fn, posargs, kwargs)

  def to_metadata(self, posarg_names, kwarg_names):
    out = {"tag": self.fn}
    for name, arg in zip(posarg_names, self.posargs):
      out[name] = arg
    for name in kwarg_names:
      if name in self.kwargs:
        out[name] = self.kwargs[name]
    return out

  def to_call_dict(self):
    return {
        "tag": "call",
        "fn": self.fn,
        "posargs": self.posargs,
        "kwargs": self.kwargs,
    }

  def to_pytd(self):
    posargs = ", ".join(map(repr, self.posargs))
    kwargs = ", ".join(f"{k}={v!r}" for k, v in self.kwargs.items())
    if posargs and kwargs:
      return f"{self.fn}({posargs}, {kwargs})"
    else:
      return f"{self.fn}({posargs}{kwargs})"


# Convert some callables to their own specific metadata dicts.
# {fn: (posarg_names, kwarg_names)}
_CALLABLES = {"Deprecated": (["reason"], [])}


def to_string(val: Any):
  return repr(val)


def from_string(val: str):
  return evaluator.eval_string_literal(val)


def call_to_annotation(fn, *, posargs=None, kwargs=None):
  """Convert a function call to a metadata dict string."""
  call = Call(fn, posargs or (), kwargs or {})
  if fn in _CALLABLES:
    posarg_names, kwarg_names = _CALLABLES[fn]
    out = call.to_metadata(posarg_names, kwarg_names)
  else:
    out = call.to_call_dict()
  return to_string(out)


def to_pytd(metadata: dict[str, Any]):
  """Convert a metadata dict to a pytd string."""
  tag = metadata.get("tag")
  if tag in _CALLABLES:
    posarg_names, kwarg_names = _CALLABLES[tag]
    return Call.from_metadata(metadata, posarg_names, kwarg_names).to_pytd()
  elif tag == "call":
    return Call.from_call_dict(metadata).to_pytd()
  else:
    return to_string(metadata)
