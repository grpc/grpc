"""Support for the 'subprocess' library."""

from pytype.abstract import abstract
from pytype.abstract import mixin
from pytype.overlays import overlay


class SubprocessOverlay(overlay.Overlay):
  """A custom overlay for the 'subprocess' module."""

  def __init__(self, ctx):
    member_map = {
        "Popen": Popen,
    }
    ast = ctx.loader.import_name("subprocess")
    super().__init__(ctx, "subprocess", member_map, ast)


class PopenInit(abstract.PyTDFunction):
  """Custom implementation of subprocess.Popen.__init__."""

  def _can_match_multiple(self, args):
    # We need to distinguish between Popen[bytes] and Popen[str]. This requires
    # an overlay because bytes/str can be distinguished definitely based on only
    # a few of the parameters, but pytype will fall back to less precise
    # matching if any of the parameters has an unknown type.
    found_ambiguous_arg = False
    for kw, literal in [
        ("encoding", False),
        ("errors", False),
        ("universal_newlines", True),
        ("text", True),
    ]:
      if kw not in args.namedargs:
        continue
      if literal:
        ambiguous = any(
            not isinstance(v, abstract.ConcreteValue)
            for v in args.namedargs[kw].data
        )
      else:
        ambiguous = any(
            isinstance(v, abstract.AMBIGUOUS_OR_EMPTY)
            for v in args.namedargs[kw].data
        )
      if not ambiguous:
        return False
      found_ambiguous_arg = True
    if found_ambiguous_arg:
      return super()._can_match_multiple(args)
    else:
      return args.has_opaque_starargs_or_starstarargs()


class Popen(abstract.PyTDClass, mixin.HasSlots):
  """Custom implementation of subprocess.Popen."""

  def __init__(self, ctx, module):
    pytd_cls = ctx.loader.lookup_pytd(module, "Popen")
    super().__init__("Popen", pytd_cls, ctx)
    mixin.HasSlots.init_mixin(self)
    self._setting_init = False  # recursion detection

  def get_special_attribute(self, node, name, valself):
    if name != "__init__" or self._setting_init:
      return super().get_special_attribute(node, name, valself)
    # lazily loaded because the signatures refer back to Popen itself
    if name not in self._slots:
      slot = self.ctx.convert.convert_pytd_function(
          self.pytd_cls.Lookup(name), PopenInit
      )
      self._setting_init = True
      self.set_slot(name, slot)
      self._setting_init = False
    return mixin.HasSlots.get_special_attribute(self, node, name, valself)
