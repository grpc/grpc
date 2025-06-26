"""Generate pytd classes for named tuples."""

from typing import Any

from pytype.pytd import escape
from pytype.pytd import pytd


class NamedTuple:
  """Construct a class for a new named tuple."""
  # This is called from the pyi parser, to convert a namedtuple constructed by a
  # functional constructor into a NamedTuple subclass.

  def __init__(self, base_name, fields, generated_classes):
    # Handle previously defined NamedTuples with the same name
    index = len(generated_classes[base_name])
    self.name = escape.pack_namedtuple_base_class(base_name, index)
    self.cls = self._new_named_tuple(self.name, fields)

  def _new_named_tuple(
      self, class_name: str, fields: list[tuple[str, Any]]
  ) -> pytd.Class:
    """Generates a pytd class for a named tuple.

    Args:
      class_name: The name of the generated class
      fields: A list of (name, type) tuples.

    Returns:
      A generated class that describes the named tuple.
    """
    class_base = pytd.NamedType("typing.NamedTuple")
    class_constants = tuple(pytd.Constant(n, t) for n, t in fields)
    return pytd.Class(name=class_name,
                      keywords=(),
                      bases=(class_base,),
                      methods=(),
                      constants=class_constants,
                      decorators=(),
                      classes=(),
                      slots=None,
                      template=())
