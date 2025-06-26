"""Implementation of special members of the future library."""

from pytype.overlays import metaclass
from pytype.overlays import overlay


class FutureUtilsOverlay(overlay.Overlay):
  """A custom overlay for the 'future' module."""

  def __init__(self, ctx):
    member_map = {
        "with_metaclass": metaclass.WithMetaclass.make,
    }
    ast = ctx.loader.import_name("future.utils")
    super().__init__(ctx, "future.utils", member_map, ast)
