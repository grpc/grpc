"""Implementation of special members of third_party/six."""

from pytype.overlays import metaclass
from pytype.overlays import overlay


class SixOverlay(overlay.Overlay):
  """A custom overlay for the 'six' module."""

  def __init__(self, ctx):
    member_map = {
        "add_metaclass": metaclass.AddMetaclass.make,
        "with_metaclass": metaclass.WithMetaclass.make,
        "string_types": overlay.drop_module(build_string_types),
        "integer_types": overlay.drop_module(build_integer_types),
        "PY2": build_version_bool(2),
        "PY3": build_version_bool(3),
    }
    ast = ctx.loader.import_name("six")
    super().__init__(ctx, "six", member_map, ast)


def build_version_bool(major):
  def make(ctx, module):
    del module  # unused
    return ctx.convert.bool_values[ctx.python_version[0] == major]

  return make


def build_string_types(ctx):
  # six.string_types is defined as a tuple, even though it's only a single value
  # in Py3.
  # We're following the pyi definition of string_types here, because the real
  # value in Py2 is `basestring`, which we don't have available.
  classes = [ctx.convert.str_type.to_variable(ctx.root_node)]
  return ctx.convert.tuple_to_value(classes)


def build_integer_types(ctx):
  # pytype treats `long` as an alias of `int`, so the value of integer_types can
  # be represented as just `(int,)` in both Py2 and Py3.
  return ctx.convert.tuple_to_value((
      ctx.convert.int_type.to_variable(ctx.root_node),
  ))
