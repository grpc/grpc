"""Handle binary operator calls."""

import logging

from pytype.rewrite.abstract import abstract
from pytype.rewrite.flow import variables

# Type aliases
_Var = variables.Variable[abstract.BaseValue]
_Binding = variables.Binding[abstract.BaseValue]

log = logging.getLogger(__name__)


def call_binary(
    ctx: abstract.ContextType,
    name: str,
    x: _Var,
    y: _Var,
) -> _Var:
  """Map a binary operator to "magic methods" (__add__ etc.)."""
  del x, y
  log.debug("Calling binary operator %s", name)
  return ctx.consts.Any.to_variable()


def call_inplace(
    ctx: abstract.ContextType,
    frame: abstract.FrameType,
    iname: str,
    x: _Var,
    y: _Var,
) -> _Var:
  """Try to call a method like __iadd__, possibly fall back to __add__."""
  assert iname.startswith("__i")
  try:
    attr = frame.load_attr(x, iname)
    del attr  # not implemented yet
  except AttributeError:
    log.info("No inplace operator %s on %r", iname, x)
    name = iname.replace("i", "", 1)  # __iadd__ -> __add__ etc.
    return call_binary(ctx, name, x, y)
  else:
    # This is an in-place call; return the lhs for now
    return x
