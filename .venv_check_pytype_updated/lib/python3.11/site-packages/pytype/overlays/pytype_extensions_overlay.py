"""Implementation of special members of pytype_extensions."""

from pytype.overlays import overlay
from pytype.overlays import special_builtins


class PytypeExtensionsOverlay(overlay.Overlay):
  """A custom overlay for the 'pytype_extensions' module."""

  def __init__(self, ctx):
    member_map = {
        "assert_type": overlay.add_name(
            "assert_type", special_builtins.AssertType.make_alias
        ),
    }
    ast = ctx.loader.import_name("pytype_extensions")
    super().__init__(ctx, "pytype_extensions", member_map, ast)
