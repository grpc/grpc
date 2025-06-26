"""Implementation of special members of typing_extensions."""

from pytype.overlays import typing_overlay


class TypingExtensionsOverlay(typing_overlay.Redirect):
  """A custom overlay for the 'typing_extensions' module."""

  def __init__(self, ctx):
    aliases = {"runtime": "typing.runtime_checkable"}
    super().__init__("typing_extensions", aliases, ctx)

  def _convert_member(self, name, member, subst=None):
    var = super()._convert_member(name, member, subst)
    for val in var.data:
      # typing_extensions backports typing features to older versions.
      # Pretending that the backports are in typing is easier than remembering
      # to check for both typing.X and typing_extensions.X every time we match
      # on an abstract value.
      val.module = "typing"
    return var
