from __future__ import annotations

import datetime
import decimal
import enum
import uuid
from collections.abc import Iterable
from typing import (
    Any,
    Final,
    Literal,
    Tuple,
    Type as typing_Type,
    TypeVar,
    Union,
)

try:
    from types import UnionType as _types_UnionType  # type: ignore
except Exception:
    _types_UnionType = type("UnionType", (), {})  # type: ignore

try:
    from typing import TypeAliasType as _TypeAliasType  # type: ignore
except Exception:
    _TypeAliasType = type("TypeAliasType", (), {})  # type: ignore

import msgspec
from msgspec import NODEFAULT, UNSET, UnsetType as _UnsetType

from ._core import (  # type: ignore
    Factory as _Factory,
    to_builtins as _to_builtins,
)
from ._utils import (  # type: ignore
    _CONCRETE_TYPES,
    _AnnotatedAlias,
    get_class_annotations as _get_class_annotations,
    get_dataclass_info as _get_dataclass_info,
    get_typeddict_info as _get_typeddict_info,
)

__all__ = (
    "type_info",
    "multi_type_info",
    "Type",
    "Metadata",
    "AnyType",
    "NoneType",
    "BoolType",
    "IntType",
    "FloatType",
    "StrType",
    "BytesType",
    "ByteArrayType",
    "MemoryViewType",
    "DateTimeType",
    "TimeType",
    "DateType",
    "TimeDeltaType",
    "UUIDType",
    "DecimalType",
    "ExtType",
    "RawType",
    "EnumType",
    "LiteralType",
    "CustomType",
    "UnionType",
    "CollectionType",
    "ListType",
    "SetType",
    "FrozenSetType",
    "VarTupleType",
    "TupleType",
    "DictType",
    "Field",
    "TypedDictType",
    "NamedTupleType",
    "DataclassType",
    "StructType",
)


def __dir__():
    return __all__


class Type(msgspec.Struct):
    """The base Type."""


class Metadata(Type):
    """A type wrapping a subtype with additional metadata.

    Parameters
    ----------
    type: Type
        The subtype.
    extra_json_schema: dict, optional
        A dict of extra fields to set for the subtype when generating a
        json-schema.
    extra: dict, optional
        A dict of extra user-defined metadata attached to the subtype.
    """

    type: Type
    extra_json_schema: Union[dict, None] = None
    extra: Union[dict, None] = None


class AnyType(Type):
    """A type corresponding to `typing.Any`."""


class NoneType(Type):
    """A type corresponding to `None`."""


class BoolType(Type):
    """A type corresponding to `bool`."""


class IntType(Type):
    """A type corresponding to `int`.

    Parameters
    ----------
    gt: int, optional
        If set, an instance of this type must be greater than ``gt``.
    ge: int, optional
        If set, an instance of this type must be greater than or equal to ``ge``.
    lt: int, optional
        If set, an instance of this type must be less than to ``lt``.
    le: int, optional
        If set, an instance of this type must be less than or equal to ``le``.
    multiple_of: int, optional
        If set, an instance of this type must be a multiple of ``multiple_of``.
    """

    gt: Union[int, None] = None
    ge: Union[int, None] = None
    lt: Union[int, None] = None
    le: Union[int, None] = None
    multiple_of: Union[int, None] = None


class FloatType(Type):
    """A type corresponding to `float`.

    Parameters
    ----------
    gt: float, optional
        If set, an instance of this type must be greater than ``gt``.
    ge: float, optional
        If set, an instance of this type must be greater than or equal to ``ge``.
    lt: float, optional
        If set, an instance of this type must be less than to ``lt``.
    le: float, optional
        If set, an instance of this type must be less than or equal to ``le``.
    multiple_of: float, optional
        If set, an instance of this type must be a multiple of ``multiple_of``.
    """

    gt: Union[float, None] = None
    ge: Union[float, None] = None
    lt: Union[float, None] = None
    le: Union[float, None] = None
    multiple_of: Union[float, None] = None


class StrType(Type):
    """A type corresponding to `str`.

    Parameters
    ----------
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    pattern: str, optional
        If set, an instance of this type must match against this regex pattern.
        Note that the pattern is treated as **unanchored**.
    """

    min_length: Union[int, None] = None
    max_length: Union[int, None] = None
    pattern: Union[str, None] = None


class BytesType(Type):
    """A type corresponding to `bytes`.

    Parameters
    ----------
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """

    min_length: Union[int, None] = None
    max_length: Union[int, None] = None


class ByteArrayType(Type):
    """A type corresponding to `bytearray`.

    Parameters
    ----------
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """

    min_length: Union[int, None] = None
    max_length: Union[int, None] = None


class MemoryViewType(Type):
    """A type corresponding to `memoryview`.

    Parameters
    ----------
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """

    min_length: Union[int, None] = None
    max_length: Union[int, None] = None


class DateTimeType(Type):
    """A type corresponding to `datetime.datetime`.

    Parameters
    ----------
    tz: bool
        The timezone-requirements for an instance of this type. ``True``
        indicates a timezone-aware value is required, ``False`` indicates a
        timezone-aware value is required. The default is ``None``, which
        accepts either timezone-aware or timezone-naive values.
    """

    tz: Union[bool, None] = None


class TimeType(Type):
    """A type corresponding to `datetime.time`.

    Parameters
    ----------
    tz: bool
        The timezone-requirements for an instance of this type. ``True``
        indicates a timezone-aware value is required, ``False`` indicates a
        timezone-aware value is required. The default is ``None``, which
        accepts either timezone-aware or timezone-naive values.
    """

    tz: Union[bool, None] = None


class DateType(Type):
    """A type corresponding to `datetime.date`."""


class TimeDeltaType(Type):
    """A type corresponding to `datetime.timedelta`."""


class UUIDType(Type):
    """A type corresponding to `uuid.UUID`."""


class DecimalType(Type):
    """A type corresponding to `decimal.Decimal`."""


class ExtType(Type):
    """A type corresponding to `msgspec.msgpack.Ext`."""


class RawType(Type):
    """A type corresponding to `msgspec.Raw`."""


class EnumType(Type):
    """A type corresponding to an `enum.Enum` type.

    Parameters
    ----------
    cls: type
        The corresponding `enum.Enum` type.
    """

    cls: typing_Type[enum.Enum]


class LiteralType(Type):
    """A type corresponding to a `typing.Literal` type.

    Parameters
    ----------
    values: tuple
        A tuple of possible values for this literal instance. Only `str` or
        `int` literals are supported.
    """

    values: Union[Tuple[str, ...], Tuple[int, ...]]


class CustomType(Type):
    """A custom type.

    Parameters
    ----------
    cls: type
        The corresponding custom type.
    """

    cls: type


class UnionType(Type):
    """A union type.

    Parameters
    ----------
    types: Tuple[Type, ...]
        A tuple of possible types for this union.
    """

    types: Tuple[Type, ...]

    @property
    def includes_none(self) -> bool:
        """A helper for checking whether ``None`` is included in this union."""
        return any(isinstance(t, NoneType) for t in self.types)


class CollectionType(Type):
    """A collection type.

    This is the base type shared by collection types like `ListType`,
    `SetType`, etc.

    Parameters
    ----------
    item_type: Type
        The item type.
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """

    item_type: Type
    min_length: Union[int, None] = None
    max_length: Union[int, None] = None


class ListType(CollectionType):
    """A type corresponding to a `list`.

    Parameters
    ----------
    item_type: Type
        The item type.
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """


class VarTupleType(CollectionType):
    """A type corresponding to a variadic `tuple`.

    Parameters
    ----------
    item_type: Type
        The item type.
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """


class SetType(CollectionType):
    """A type corresponding to a `set`.

    Parameters
    ----------
    item_type: Type
        The item type.
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """


class FrozenSetType(CollectionType):
    """A type corresponding to a `frozenset`.

    Parameters
    ----------
    item_type: Type
        The item type.
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """


class TupleType(Type):
    """A type corresponding to `tuple`.

    Parameters
    ----------
    item_types: Tuple[Type, ...]
        A tuple of types for each element in the tuple.
    """

    item_types: Tuple[Type, ...]


class DictType(Type):
    """A type corresponding to `dict`.

    Parameters
    ----------
    key_type: Type
        The key type.
    value_type: Type
        The value type.
    min_length: int, optional
        If set, an instance of this type must have length greater than or equal
        to ``min_length``.
    max_length: int, optional
        If set, an instance of this type must have length less than or equal
        to ``max_length``.
    """

    key_type: Type
    value_type: Type
    min_length: Union[int, None] = None
    max_length: Union[int, None] = None


class Field(msgspec.Struct):
    """A record describing a field in an object-like type.

    Parameters
    ----------
    name: str
        The field name as seen by Python code (e.g. ``field_one``).
    encode_name: str
        The name used when encoding/decoding the field. This may differ if
        the field is renamed (e.g. ``fieldOne``).
    type: Type
        The field type.
    required: bool, optional
        Whether the field is required. Note that if `required` is False doesn't
        necessarily mean that `default` or `default_factory` will be set -
        optional fields may exist with no default value.
    default: Any, optional
        A default value for the field. Will be `NODEFAULT` if no default value
        is set.
    default_factory: Any, optional
        A callable that creates a default value for the field. Will be
        `NODEFAULT` if no ``default_factory`` is set.
    """

    name: str
    encode_name: str
    type: Type
    required: bool = True
    default: Any = msgspec.field(default_factory=lambda: NODEFAULT)
    default_factory: Any = msgspec.field(default_factory=lambda: NODEFAULT)


class TypedDictType(Type):
    """A type corresponding to a `typing.TypedDict` type.

    Parameters
    ----------
    cls: type
        The corresponding TypedDict type.
    fields: Tuple[Field, ...]
        A tuple of fields in the TypedDict.
    """

    cls: type
    fields: Tuple[Field, ...]


class NamedTupleType(Type):
    """A type corresponding to a `typing.NamedTuple` type.

    Parameters
    ----------
    cls: type
        The corresponding NamedTuple type.
    fields: Tuple[Field, ...]
        A tuple of fields in the NamedTuple.
    """

    cls: type
    fields: Tuple[Field, ...]


class DataclassType(Type):
    """A type corresponding to a `dataclasses` or `attrs` type.

    Parameters
    ----------
    cls: type
        The corresponding dataclass type.
    fields: Tuple[Field, ...]
        A tuple of fields in the dataclass.
    """

    cls: type
    fields: Tuple[Field, ...]


class StructType(Type):
    """A type corresponding to a `msgspec.Struct` type.

    Parameters
    ----------
    cls: type
        The corresponding Struct type.
    fields: Tuple[Field, ...]
        A tuple of fields in the Struct.
    tag_field: str or None, optional
        If set, the field name used for the tag in a tagged union.
    tag: str, int, or None, optional
        If set, the value used for the tag in a tagged union.
    array_like: bool, optional
        Whether the struct is encoded as an array rather than an object.
    forbid_unknown_fields: bool, optional
        If ``False`` (the default) unknown fields are ignored when decoding. If
        ``True`` any unknown fields will result in an error.
    """

    cls: typing_Type[msgspec.Struct]
    fields: Tuple[Field, ...]
    tag_field: Union[str, None] = None
    tag: Union[str, int, None] = None
    array_like: bool = False
    forbid_unknown_fields: bool = False


def multi_type_info(types: Iterable[Any]) -> tuple[Type, ...]:
    """Get information about multiple msgspec-compatible types.

    Parameters
    ----------
    types: an iterable of types
        The types to get info about.

    Returns
    -------
    tuple[Type, ...]

    Examples
    --------
    >>> msgspec.inspect.multi_type_info([int, float, list[str]])  # doctest: +NORMALIZE_WHITESPACE
    (IntType(gt=None, ge=None, lt=None, le=None, multiple_of=None),
     FloatType(gt=None, ge=None, lt=None, le=None, multiple_of=None),
     ListType(item_type=StrType(min_length=None, max_length=None, pattern=None),
              min_length=None, max_length=None))
    """
    return _Translator(types).run()


def type_info(type: Any) -> Type:
    """Get information about a msgspec-compatible type.

    Note that if you need to inspect multiple types it's more efficient to call
    `multi_type_info` once with a sequence of types than calling `type_info`
    multiple times.

    Parameters
    ----------
    type: type
        The type to get info about.

    Returns
    -------
    Type

    Examples
    --------
    >>> msgspec.inspect.type_info(bool)
    BoolType()

    >>> msgspec.inspect.type_info(int)
    IntType(gt=None, ge=None, lt=None, le=None, multiple_of=None)

    >>> msgspec.inspect.type_info(list[int])  # doctest: +NORMALIZE_WHITESPACE
    ListType(item_type=IntType(gt=None, ge=None, lt=None, le=None, multiple_of=None),
             min_length=None, max_length=None)
    """
    return multi_type_info([type])[0]


# Implementation details
def _origin_args_metadata(t):
    # Strip wrappers (Annotated, NewType, Final) until we hit a concrete type
    metadata = []
    while True:
        try:
            origin = _CONCRETE_TYPES.get(t)
        except TypeError:
            # t is not hashable
            origin = None

        if origin is not None:
            args = None
            break

        origin = getattr(t, "__origin__", None)
        if origin is not None:
            if type(t) is _AnnotatedAlias:
                metadata.extend(m for m in t.__metadata__ if type(m) is msgspec.Meta)
                t = origin
            elif origin == Final:
                t = t.__args__[0]
            elif type(origin) is _TypeAliasType:
                t = origin.__value__[t.__args__]
            else:
                args = getattr(t, "__args__", None)
                origin = _CONCRETE_TYPES.get(origin, origin)
                break
        else:
            supertype = getattr(t, "__supertype__", None)
            if supertype is not None:
                t = supertype
            elif type(t) is _TypeAliasType:
                t = t.__value__
            else:
                origin = t
                args = None
                break

    if type(origin) is _types_UnionType:
        args = origin.__args__
        origin = Union
    return origin, args, tuple(metadata)


def _is_struct(t):
    return type(t) is type(msgspec.Struct)


def _is_enum(t):
    return type(t) is enum.EnumMeta


def _is_dataclass(t):
    return hasattr(t, "__dataclass_fields__")


def _is_attrs(t):
    return hasattr(t, "__attrs_attrs__")


def _is_typeddict(t):
    try:
        return issubclass(t, dict) and hasattr(t, "__total__")
    except TypeError:
        return False


def _is_namedtuple(t):
    try:
        return issubclass(t, tuple) and hasattr(t, "_fields")
    except TypeError:
        return False


def _merge_json(a, b):
    if b:
        a = a.copy()
        for key, b_val in b.items():
            if key in a:
                a_val = a[key]
                if isinstance(a_val, dict) and isinstance(b_val, dict):
                    a[key] = _merge_json(a_val, b_val)
                elif isinstance(a_val, (list, tuple)) and isinstance(
                    b_val, (list, tuple)
                ):
                    a[key] = list(a_val) + list(b_val)
                else:
                    a[key] = b_val
            else:
                a[key] = b_val
    return a


class _Translator:
    def __init__(self, types):
        self.types = tuple(types)
        self.type_hints = {}
        self.cache = {}

    def _get_class_annotations(self, t):
        """A cached version of `get_class_annotations`"""
        try:
            return self.type_hints[t]
        except KeyError:
            out = self.type_hints[t] = _get_class_annotations(t)
            return out

    def run(self):
        # First construct a decoder to validate the types are valid
        from ._core import MsgpackDecoder

        MsgpackDecoder(Tuple[self.types])
        return tuple(self.translate(t) for t in self.types)

    def translate(self, typ):
        t, args, metadata = _origin_args_metadata(typ)

        # Extract and merge components of any `Meta` annotations
        constrs = {}
        extra_json_schema = {}
        extra = {}
        for meta in metadata:
            for attr in (
                "ge",
                "gt",
                "le",
                "lt",
                "multiple_of",
                "pattern",
                "min_length",
                "max_length",
                "tz",
            ):
                if (val := getattr(meta, attr)) is not None:
                    constrs[attr] = val
            for attr in ("title", "description", "examples"):
                if (val := getattr(meta, attr)) is not None:
                    extra_json_schema[attr] = val
            if meta.extra_json_schema is not None:
                extra_json_schema = _merge_json(
                    extra_json_schema,
                    _to_builtins(meta.extra_json_schema, str_keys=True),
                )
            if meta.extra is not None:
                extra.update(meta.extra)

        out = self._translate_inner(t, args, **constrs)
        if extra_json_schema or extra:
            # If extra metadata is present, wrap the output type in a Metadata
            # wrapper object
            return Metadata(
                out, extra_json_schema=extra_json_schema or None, extra=extra or None
            )
        return out

    def _translate_inner(
        self,
        t,
        args,
        ge=None,
        gt=None,
        le=None,
        lt=None,
        multiple_of=None,
        pattern=None,
        min_length=None,
        max_length=None,
        tz=None,
    ):
        if t is Any:
            return AnyType()
        elif isinstance(t, TypeVar):
            if t.__bound__ is not None:
                return self.translate(t.__bound__)
            return AnyType()
        elif t is None or t is type(None):
            return NoneType()
        elif t is bool:
            return BoolType()
        elif t is int:
            return IntType(ge=ge, gt=gt, le=le, lt=lt, multiple_of=multiple_of)
        elif t is float:
            return FloatType(ge=ge, gt=gt, le=le, lt=lt, multiple_of=multiple_of)
        elif t is str:
            return StrType(
                min_length=min_length, max_length=max_length, pattern=pattern
            )
        elif t is bytes:
            return BytesType(min_length=min_length, max_length=max_length)
        elif t is bytearray:
            return ByteArrayType(min_length=min_length, max_length=max_length)
        elif t is memoryview:
            return MemoryViewType(min_length=min_length, max_length=max_length)
        elif t is datetime.datetime:
            return DateTimeType(tz=tz)
        elif t is datetime.time:
            return TimeType(tz=tz)
        elif t is datetime.date:
            return DateType()
        elif t is datetime.timedelta:
            return TimeDeltaType()
        elif t is uuid.UUID:
            return UUIDType()
        elif t is decimal.Decimal:
            return DecimalType()
        elif t is msgspec.Raw:
            return RawType()
        elif t is msgspec.msgpack.Ext:
            return ExtType()
        elif t is list:
            return ListType(
                self.translate(args[0]) if args else AnyType(),
                min_length=min_length,
                max_length=max_length,
            )
        elif t is set:
            return SetType(
                self.translate(args[0]) if args else AnyType(),
                min_length=min_length,
                max_length=max_length,
            )
        elif t is frozenset:
            return FrozenSetType(
                self.translate(args[0]) if args else AnyType(),
                min_length=min_length,
                max_length=max_length,
            )
        elif t is tuple:
            # Handle an annoying compatibility issue:
            # - Tuple[()] has args == ((),)
            # - tuple[()] has args == ()
            if args == ((),):
                args = ()
            if args is None:
                return VarTupleType(
                    AnyType(), min_length=min_length, max_length=max_length
                )
            elif len(args) == 2 and args[-1] is ...:
                return VarTupleType(
                    self.translate(args[0]),
                    min_length=min_length,
                    max_length=max_length,
                )
            else:
                return TupleType(tuple(self.translate(a) for a in args))
        elif t is dict:
            return DictType(
                self.translate(args[0]) if args else AnyType(),
                self.translate(args[1]) if args else AnyType(),
                min_length=min_length,
                max_length=max_length,
            )
        elif t is Union:
            args = tuple(self.translate(a) for a in args if a is not _UnsetType)
            return args[0] if len(args) == 1 else UnionType(args)
        elif t is Literal:
            return LiteralType(tuple(sorted(args)))
        elif _is_enum(t):
            return EnumType(t)
        elif _is_struct(t):
            cls = t[args] if args else t
            if cls in self.cache:
                return self.cache[cls]
            config = t.__struct_config__
            self.cache[cls] = out = StructType(
                cls,
                (),
                tag_field=config.tag_field,
                tag=config.tag,
                array_like=config.array_like,
                forbid_unknown_fields=config.forbid_unknown_fields,
            )

            hints = self._get_class_annotations(cls)
            npos = len(t.__struct_fields__) - len(t.__struct_defaults__)
            fields = []
            for name, encode_name, default_obj in zip(
                t.__struct_fields__,
                t.__struct_encode_fields__,
                (NODEFAULT,) * npos + t.__struct_defaults__,
            ):
                if default_obj is NODEFAULT:
                    required = True
                    default = default_factory = NODEFAULT
                elif isinstance(default_obj, _Factory):
                    required = False
                    default = NODEFAULT
                    default_factory = default_obj.factory
                else:
                    required = False
                    default = NODEFAULT if default_obj is UNSET else default_obj
                    default_factory = NODEFAULT

                field = Field(
                    name=name,
                    encode_name=encode_name,
                    type=self.translate(hints[name]),
                    required=required,
                    default=default,
                    default_factory=default_factory,
                )
                fields.append(field)

            out.fields = tuple(fields)
            return out
        elif _is_typeddict(t):
            cls = t[args] if args else t
            if cls in self.cache:
                return self.cache[cls]
            self.cache[cls] = out = TypedDictType(cls, ())
            hints, required = _get_typeddict_info(cls)
            out.fields = tuple(
                Field(
                    name=name,
                    encode_name=name,
                    type=self.translate(field_type),
                    required=name in required,
                )
                for name, field_type in sorted(hints.items())
            )
            return out
        elif _is_dataclass(t) or _is_attrs(t):
            cls = t[args] if args else t
            if cls in self.cache:
                return self.cache[cls]
            self.cache[cls] = out = DataclassType(cls, ())
            _, info, defaults, _, _ = _get_dataclass_info(cls)
            defaults = ((NODEFAULT,) * (len(info) - len(defaults))) + defaults
            fields = []
            for (name, typ, is_factory), default_obj in zip(info, defaults):
                if default_obj is NODEFAULT:
                    required = True
                    default = default_factory = NODEFAULT
                elif is_factory:
                    required = False
                    default = NODEFAULT
                    default_factory = default_obj
                else:
                    required = False
                    default = NODEFAULT if default_obj is UNSET else default_obj
                    default_factory = NODEFAULT

                fields.append(
                    Field(
                        name=name,
                        encode_name=name,
                        type=self.translate(typ),
                        required=required,
                        default=default,
                        default_factory=default_factory,
                    )
                )
            out.fields = tuple(fields)
            return out
        elif _is_namedtuple(t):
            cls = t[args] if args else t
            if cls in self.cache:
                return self.cache[cls]
            self.cache[cls] = out = NamedTupleType(cls, ())
            hints = self._get_class_annotations(cls)
            out.fields = tuple(
                Field(
                    name=name,
                    encode_name=name,
                    type=self.translate(hints.get(name, Any)),
                    required=name not in t._field_defaults,
                    default=t._field_defaults.get(name, NODEFAULT),
                )
                for name in t._fields
            )
            return out
        else:
            return CustomType(t)
