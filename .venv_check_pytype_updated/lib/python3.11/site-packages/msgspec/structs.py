from __future__ import annotations

from typing import Any

from . import NODEFAULT, Struct, field
from ._core import (  # noqa
    Factory as _Factory,
    StructConfig,
    asdict,
    astuple,
    replace,
    force_setattr,
)
from ._utils import get_class_annotations as _get_class_annotations

__all__ = (
    "FieldInfo",
    "StructConfig",
    "asdict",
    "astuple",
    "fields",
    "force_setattr",
    "replace",
)


def __dir__():
    return __all__


class FieldInfo(Struct):
    """A record describing a field in a struct type.

    Parameters
    ----------
    name: str
        The field name as seen by Python code (e.g. ``field_one``).
    encode_name: str
        The name used when encoding/decoding the field. This may differ if
        the field is renamed (e.g. ``fieldOne``).
    type: Any
        The full field type annotation.
    default: Any, optional
        A default value for the field. Will be `NODEFAULT` if no default value
        is set.
    default_factory: Any, optional
        A callable that creates a default value for the field. Will be
        `NODEFAULT` if no ``default_factory`` is set.
    """

    name: str
    encode_name: str
    type: Any
    default: Any = field(default_factory=lambda: NODEFAULT)
    default_factory: Any = field(default_factory=lambda: NODEFAULT)

    @property
    def required(self) -> bool:
        """A helper for checking whether a field is required"""
        return self.default is NODEFAULT and self.default_factory is NODEFAULT


def fields(type_or_instance: Struct | type[Struct]) -> tuple[FieldInfo]:
    """Get information about the fields in a Struct.

    Parameters
    ----------
    type_or_instance:
        A struct type or instance.

    Returns
    -------
    tuple[FieldInfo]
    """
    if isinstance(type_or_instance, Struct):
        annotated_cls = cls = type(type_or_instance)
    else:
        annotated_cls = type_or_instance
        cls = getattr(type_or_instance, "__origin__", type_or_instance)
        if not (isinstance(cls, type) and issubclass(cls, Struct)):
            raise TypeError("Must be called with a struct type or instance")

    hints = _get_class_annotations(annotated_cls)
    npos = len(cls.__struct_fields__) - len(cls.__struct_defaults__)
    fields = []
    for name, encode_name, default_obj in zip(
        cls.__struct_fields__,
        cls.__struct_encode_fields__,
        (NODEFAULT,) * npos + cls.__struct_defaults__,
    ):
        default = default_factory = NODEFAULT
        if isinstance(default_obj, _Factory):
            default_factory = default_obj.factory
        elif default_obj is not NODEFAULT:
            default = default_obj

        field = FieldInfo(
            name=name,
            encode_name=encode_name,
            type=hints[name],
            default=default,
            default_factory=default_factory,
        )
        fields.append(field)

    return tuple(fields)
