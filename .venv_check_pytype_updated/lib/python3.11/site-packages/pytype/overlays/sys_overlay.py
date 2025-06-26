"""Implementation of special members of sys."""

from pytype.abstract import abstract
from pytype.overlays import overlay


class SysOverlay(overlay.Overlay):
  """A custom overlay for the 'sys' module."""

  def __init__(self, ctx):
    member_map = {
        "platform": overlay.drop_module(build_platform),
        "version_info": overlay.drop_module(build_version_info),
    }
    ast = ctx.loader.import_name("sys")
    super().__init__(ctx, "sys", member_map, ast)


class VersionInfo(abstract.Tuple):

  ATTRIBUTES = ("major", "minor", "micro", "releaselevel", "serial")

  def get_special_attribute(self, node, name, valself):
    try:
      index = self.ATTRIBUTES.index(name)
    except ValueError:
      return None
    return self.pyval[index]


def build_platform(ctx):
  return ctx.convert.constant_to_value(ctx.options.platform)


def build_version_info(ctx):
  """Build sys.version_info."""
  version = []
  # major, minor
  for i in ctx.python_version:
    version.append(ctx.convert.constant_to_var(i))
  # micro, releaselevel, serial
  for t in (int, str, int):
    version.append(
        ctx.convert.primitive_instances[t].to_variable(ctx.root_node)
    )
  return VersionInfo(tuple(version), ctx)
