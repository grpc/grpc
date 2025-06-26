# This file is derived from github.com/ericvsmith/dataclasses, and is Apache 2 licensed.
# https://github.com/ericvsmith/dataclasses/blob/ae712dd993420d43444f188f452/LICENSE.txt
# https://github.com/ericvsmith/dataclasses/blob/ae712dd993420d43444f/dataclass_tools.py
# Changed: takes slots in base classes into account when creating slots

import dataclasses
from itertools import chain, filterfalse
from typing import Any, Mapping, Type, TypeVar

_T = TypeVar("_T")


def add_slots(cls: Type[_T]) -> Type[_T]:
    # Need to create a new class, since we can't set __slots__
    #  after a class has been created.

    # Make sure __slots__ isn't already set.
    if "__slots__" in cls.__dict__:
        raise TypeError(f"{cls.__name__} already specifies __slots__")

    # Create a new dict for our new class.
    cls_dict = dict(cls.__dict__)
    field_names = tuple(f.name for f in dataclasses.fields(cls))
    inherited_slots = set(
        chain.from_iterable(
            superclass.__dict__.get("__slots__", ()) for superclass in cls.mro()
        )
    )
    cls_dict["__slots__"] = tuple(
        filterfalse(inherited_slots.__contains__, field_names)
    )
    for field_name in field_names:
        # Remove our attributes, if present. They'll still be
        #  available in _MARKER.
        cls_dict.pop(field_name, None)
    # Remove __dict__ itself.
    cls_dict.pop("__dict__", None)

    # Create the class.
    qualname = getattr(cls, "__qualname__", None)

    # pyre-fixme[9]: cls has type `Type[Variable[_T]]`; used as `_T`.
    # pyre-fixme[19]: Expected 0 positional arguments.
    cls = type(cls)(cls.__name__, cls.__bases__, cls_dict)
    if qualname is not None:
        cls.__qualname__ = qualname

    # Set __getstate__ and __setstate__ to workaround a bug with pickling frozen
    # dataclasses with slots. See https://bugs.python.org/issue36424

    def __getstate__(self: object) -> Mapping[str, Any]:
        return {
            field.name: getattr(self, field.name)
            for field in dataclasses.fields(self)
            if hasattr(self, field.name)
        }

    def __setstate__(self: object, state: Mapping[str, Any]) -> None:
        for fieldname, value in state.items():
            object.__setattr__(self, fieldname, value)

    cls.__getstate__ = __getstate__
    cls.__setstate__ = __setstate__

    return cls
