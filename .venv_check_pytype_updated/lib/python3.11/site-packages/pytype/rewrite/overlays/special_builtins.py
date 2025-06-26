"""Builtin values with special behavior."""

from collections.abc import Sequence

from pytype.rewrite.abstract import abstract
from pytype.rewrite.overlays import overlays


def _stack(
    frame: abstract.FrameType | None,
) -> Sequence[abstract.FrameType] | None:
  return frame.stack if frame else None


@overlays.register_function('builtins', 'assert_type')
class AssertType(abstract.PytdFunction):
  """assert_type implementation."""

  def call_with_mapped_args(
      self, mapped_args: abstract.MappedArgs[abstract.FrameType],
  ) -> abstract.SimpleReturn:
    var = mapped_args.argdict['val']
    typ = mapped_args.argdict['typ']
    pp = self._ctx.errorlog.pretty_printer
    actual = pp.print_var_type(var, node=None)
    try:
      expected = abstract.get_atomic_constant(typ, str)
    except ValueError:
      expected = pp.print_type_of_instance(typ.get_atomic_value())
    if actual != expected:
      stack = _stack(mapped_args.frame)
      self._ctx.errorlog.assert_type(stack, actual, expected)
    return abstract.SimpleReturn(self._ctx.consts[None])


@overlays.register_function('builtins', 'reveal_type')
class RevealType(abstract.PytdFunction):
  """reveal_type implementation."""

  def call_with_mapped_args(
      self, mapped_args: abstract.MappedArgs[abstract.FrameType],
  ) -> abstract.SimpleReturn:
    obj = mapped_args.argdict['obj']
    stack = _stack(mapped_args.frame)
    self._ctx.errorlog.reveal_type(stack, node=None, var=obj)
    return abstract.SimpleReturn(self._ctx.consts[None])
