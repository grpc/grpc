"""Hierarchy of abstract base classes, from _collections_abc.py."""

from pytype import utils


# class -> list of superclasses
SUPERCLASSES = {
    # "mixins" (don't derive from object):
    "Hashable": [],
    "Iterable": [],
    "AsyncIterable": [],
    "Sized": [],
    "Callable": [],
    "Awaitable": [],
    "Iterator": ["Iterable"],
    "AsyncIterator": ["AsyncIterable"],
    "Coroutine": ["Awaitable"],
    # Classes (derive from object):
    "Container": ["object"],
    "Number": ["object"],
    "Complex": ["Number"],
    "Real": ["Complex"],
    "Rational": ["Real"],
    "Integral": ["Rational"],
    "Set": ["Sized", "Iterable", "Container"],
    "MutableSet": ["Set"],
    "Mapping": ["Sized", "Iterable", "Container"],
    "MappingView": ["Sized"],
    "KeysView": ["MappingView", "Set"],
    "ItemsView": ["MappingView", "Set"],
    "ValuesView": ["MappingView"],
    "MutableMapping": ["Mapping"],
    "Sequence": ["Sized", "Iterable", "Container"],
    "MutableSequence": ["Sequence"],
    "ByteString": ["Sequence"],
    # Builtin types:
    "set": ["MutableSet"],
    "frozenset": ["Set"],
    "dict": ["MutableMapping"],
    "tuple": ["Sequence"],
    "list": ["MutableSequence"],
    "complex": ["Complex"],
    "float": ["Real"],
    "int": ["Integral"],
    "bool": ["int"],
    "str": ["Sequence"],
    "basestring": ["Sequence"],
    "bytes": ["ByteString"],
    "range": ["Sequence"],
    "bytearray": ["ByteString", "MutableSequence"],
    "memoryview": ["Sequence"],
    # Types that can only be constructed indirectly:
    # (See EOL comments for the definition)
    "bytearray_iterator": ["Iterator"],  # type(iter(bytearray()))
    "dict_keys": ["KeysView"],  # type({}.keys()).
    "dict_items": ["ItemsView"],  # type({}.items()).
    "dict_values": ["ValuesView"],  # type({}.values())
    "dict_keyiterator": ["Iterator"],  # type(iter({}.keys()))
    "dict_valueiterator": ["Iterator"],  # type(iter({}.values()))
    "dict_itemiterator": ["Iterator"],  # type(iter({}.items()))
    "list_iterator": ["Iterator"],  # type(iter([]))
    "list_reverseiterator": ["Iterator"],  # type(iter(reversed([])))
    "range_iterator": ["Iterator"],  # type(iter(range(0)))
    "longrange_iterator": ["Iterator"],  # type(iter(range(1 << 1000)))
    "set_iterator": ["Iterator"],  # type(iter(set()))
    "tuple_iterator": ["Iterator"],  # type(iter(()))
    "str_iterator": ["Iterator"],  # type(iter("")).
    "zip_iterator": ["Iterator"],  # type(iter(zip())).
    "bytes_iterator": ["Iterator"],  # type(iter(b'')).
    "mappingproxy": ["Mapping"],  # type(type.__dict__)
    "generator": ["Generator"],  # type((lambda: (yield))())
    "async_generator": ["AsyncGenerator"],  # type((lambda: (yield))())
    "coroutine": ["Coroutine"],
}


def GetSuperClasses():
  """Get a Python type hierarchy mapping.

  This generates a dictionary that can be used to look up the bases of
  a type in the abstract base class hierarchy.

  Returns:
    A dictionary mapping a type, as string, to a list of base types (also
    as strings). E.g. "float" -> ["Real"].
  """

  return SUPERCLASSES.copy()


def GetSubClasses():
  """Get a reverse Python type hierarchy mapping.

  This generates a dictionary that can be used to look up the (known)
  subclasses of a type in the abstract base class hierarchy.

  Returns:
    A dictionary mapping a type, as string, to a list of direct
    subclasses (also as strings).
    E.g. "Sized" -> ["Set", "Mapping", "MappingView", "Sequence"].
  """

  return utils.invert_dict(GetSuperClasses())
