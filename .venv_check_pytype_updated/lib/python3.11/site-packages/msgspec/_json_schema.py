from __future__ import annotations

import re
import textwrap
from collections.abc import Iterable
from typing import Any, Optional, Callable

from . import inspect as mi, to_builtins

__all__ = ("schema", "schema_components")


def schema(
    type: Any, *, schema_hook: Optional[Callable[[type], dict[str, Any]]] = None
) -> dict[str, Any]:
    """Generate a JSON Schema for a given type.

    Any schemas for (potentially) shared components are extracted and stored in
    a top-level ``"$defs"`` field.

    If you want to generate schemas for multiple types, or to have more control
    over the generated schema you may want to use ``schema_components`` instead.

    Parameters
    ----------
    type : type
        The type to generate the schema for.
    schema_hook : callable, optional
        An optional callback to use for generating JSON schemas of custom
        types. Will be called with the custom type, and should return a dict
        representation of the JSON schema for that type.

    Returns
    -------
    schema : dict
        The generated JSON Schema.

    See Also
    --------
    schema_components
    """
    (out,), components = schema_components((type,), schema_hook=schema_hook)
    if components:
        out["$defs"] = components
    return out


def schema_components(
    types: Iterable[Any],
    *,
    schema_hook: Optional[Callable[[type], dict[str, Any]]] = None,
    ref_template: str = "#/$defs/{name}",
) -> tuple[tuple[dict[str, Any], ...], dict[str, Any]]:
    """Generate JSON Schemas for one or more types.

    Any schemas for (potentially) shared components are extracted and returned
    in a separate ``components`` dict.

    Parameters
    ----------
    types : Iterable[type]
        An iterable of one or more types to generate schemas for.
    schema_hook : callable, optional
        An optional callback to use for generating JSON schemas of custom
        types. Will be called with the custom type, and should return a dict
        representation of the JSON schema for that type.
    ref_template : str, optional
        A template to use when generating ``"$ref"`` fields. This template is
        formatted with the type name as ``template.format(name=name)``. This
        can be useful if you intend to store the ``components`` mapping
        somewhere other than a top-level ``"$defs"`` field. For example, you
        might use ``ref_template="#/components/{name}"`` if generating an
        OpenAPI schema.

    Returns
    -------
    schemas : tuple[dict]
        A tuple of JSON Schemas, one for each type in ``types``.
    components : dict
        A mapping of name to schema for any shared components used by
        ``schemas``.

    See Also
    --------
    schema
    """
    type_infos = mi.multi_type_info(types)

    component_types = _collect_component_types(type_infos)

    name_map = _build_name_map(component_types)

    gen = _SchemaGenerator(name_map, schema_hook, ref_template)

    schemas = tuple(gen.to_schema(t) for t in type_infos)

    components = {
        name_map[cls]: gen.to_schema(t, False) for cls, t in component_types.items()
    }
    return schemas, components


def _collect_component_types(type_infos: Iterable[mi.Type]) -> dict[Any, mi.Type]:
    """Find all types in the type tree that are "nameable" and worthy of being
    extracted out into a shared top-level components mapping.

    Currently this looks for Struct, Dataclass, NamedTuple, TypedDict, and Enum
    types.
    """
    components = {}

    def collect(t):
        if isinstance(
            t, (mi.StructType, mi.TypedDictType, mi.DataclassType, mi.NamedTupleType)
        ):
            if t.cls not in components:
                components[t.cls] = t
                for f in t.fields:
                    collect(f.type)
        elif isinstance(t, mi.EnumType):
            components[t.cls] = t
        elif isinstance(t, mi.Metadata):
            collect(t.type)
        elif isinstance(t, mi.CollectionType):
            collect(t.item_type)
        elif isinstance(t, mi.TupleType):
            for st in t.item_types:
                collect(st)
        elif isinstance(t, mi.DictType):
            collect(t.key_type)
            collect(t.value_type)
        elif isinstance(t, mi.UnionType):
            for st in t.types:
                collect(st)

    for t in type_infos:
        collect(t)

    return components


def _type_repr(obj):
    return obj.__name__ if isinstance(obj, type) else repr(obj)


def _get_class_name(cls: Any) -> str:
    if hasattr(cls, "__origin__"):
        name = cls.__origin__.__name__
        args = ", ".join(_type_repr(a) for a in cls.__args__)
        return f"{name}[{args}]"
    return cls.__name__


def _get_doc(t: mi.Type) -> str:
    assert hasattr(t, "cls")
    cls = getattr(t.cls, "__origin__", t.cls)
    doc = getattr(cls, "__doc__", "")
    if not doc:
        return ""
    doc = textwrap.dedent(doc).strip("\r\n")
    if isinstance(t, mi.EnumType):
        if doc == "An enumeration.":
            return ""
    elif isinstance(t, (mi.NamedTupleType, mi.DataclassType)):
        if doc.startswith(f"{cls.__name__}(") and doc.endswith(")"):
            return ""
    return doc


def _build_name_map(component_types: dict[Any, mi.Type]) -> dict[Any, str]:
    """A mapping from nameable subcomponents to a generated name.

    The generated name is usually a normalized version of the class name. In
    the case of conflicts, the name will be expanded to also include the full
    import path.
    """

    def normalize(name):
        return re.sub(r"[^a-zA-Z0-9.\-_]", "_", name)

    def fullname(cls):
        return normalize(f"{cls.__module__}.{cls.__qualname__}")

    conflicts = set()
    names: dict[str, Any] = {}

    for cls in component_types:
        name = normalize(_get_class_name(cls))
        if name in names:
            old = names.pop(name)
            conflicts.add(name)
            names[fullname(old)] = old
        if name in conflicts:
            names[fullname(cls)] = cls
        else:
            names[name] = cls
    return {v: k for k, v in names.items()}


class _SchemaGenerator:
    def __init__(
        self,
        name_map: dict[Any, str],
        schema_hook: Optional[Callable[[type], dict[str, Any]]] = None,
        ref_template: str = "#/$defs/{name}",
    ):
        self.name_map = name_map
        self.schema_hook = schema_hook
        self.ref_template = ref_template

    def to_schema(self, t: mi.Type, check_ref: bool = True) -> dict[str, Any]:
        """Converts a Type to a json-schema."""
        schema: dict[str, Any] = {}

        while isinstance(t, mi.Metadata):
            schema = mi._merge_json(schema, t.extra_json_schema)
            t = t.type

        if check_ref and hasattr(t, "cls"):
            if name := self.name_map.get(t.cls):
                schema["$ref"] = self.ref_template.format(name=name)
                return schema

        if isinstance(t, (mi.AnyType, mi.RawType)):
            pass
        elif isinstance(t, mi.NoneType):
            schema["type"] = "null"
        elif isinstance(t, mi.BoolType):
            schema["type"] = "boolean"
        elif isinstance(t, (mi.IntType, mi.FloatType)):
            schema["type"] = "integer" if isinstance(t, mi.IntType) else "number"
            if t.ge is not None:
                schema["minimum"] = t.ge
            if t.gt is not None:
                schema["exclusiveMinimum"] = t.gt
            if t.le is not None:
                schema["maximum"] = t.le
            if t.lt is not None:
                schema["exclusiveMaximum"] = t.lt
            if t.multiple_of is not None:
                schema["multipleOf"] = t.multiple_of
        elif isinstance(t, mi.StrType):
            schema["type"] = "string"
            if t.max_length is not None:
                schema["maxLength"] = t.max_length
            if t.min_length is not None:
                schema["minLength"] = t.min_length
            if t.pattern is not None:
                schema["pattern"] = t.pattern
        elif isinstance(t, (mi.BytesType, mi.ByteArrayType, mi.MemoryViewType)):
            schema["type"] = "string"
            schema["contentEncoding"] = "base64"
            if t.max_length is not None:
                schema["maxLength"] = 4 * ((t.max_length + 2) // 3)
            if t.min_length is not None:
                schema["minLength"] = 4 * ((t.min_length + 2) // 3)
        elif isinstance(t, mi.DateTimeType):
            schema["type"] = "string"
            if t.tz is True:
                schema["format"] = "date-time"
        elif isinstance(t, mi.TimeType):
            schema["type"] = "string"
            if t.tz is True:
                schema["format"] = "time"
            elif t.tz is False:
                schema["format"] = "partial-time"
        elif isinstance(t, mi.DateType):
            schema["type"] = "string"
            schema["format"] = "date"
        elif isinstance(t, mi.TimeDeltaType):
            schema["type"] = "string"
            schema["format"] = "duration"
        elif isinstance(t, mi.UUIDType):
            schema["type"] = "string"
            schema["format"] = "uuid"
        elif isinstance(t, mi.DecimalType):
            schema["type"] = "string"
            schema["format"] = "decimal"
        elif isinstance(t, mi.CollectionType):
            schema["type"] = "array"
            if not isinstance(t.item_type, mi.AnyType):
                schema["items"] = self.to_schema(t.item_type)
            if t.max_length is not None:
                schema["maxItems"] = t.max_length
            if t.min_length is not None:
                schema["minItems"] = t.min_length
        elif isinstance(t, mi.TupleType):
            schema["type"] = "array"
            schema["minItems"] = schema["maxItems"] = len(t.item_types)
            if t.item_types:
                schema["prefixItems"] = [self.to_schema(i) for i in t.item_types]
                schema["items"] = False
        elif isinstance(t, mi.DictType):
            schema["type"] = "object"
            # If there are restrictions on the keys, specify them as propertyNames
            if isinstance(key_type := t.key_type, mi.StrType):
                property_names: dict[str, Any] = {}
                if key_type.min_length is not None:
                    property_names["minLength"] = key_type.min_length
                if key_type.max_length is not None:
                    property_names["maxLength"] = key_type.max_length
                if key_type.pattern is not None:
                    property_names["pattern"] = key_type.pattern
                if property_names:
                    schema["propertyNames"] = property_names
            if not isinstance(t.value_type, mi.AnyType):
                schema["additionalProperties"] = self.to_schema(t.value_type)
            if t.max_length is not None:
                schema["maxProperties"] = t.max_length
            if t.min_length is not None:
                schema["minProperties"] = t.min_length
        elif isinstance(t, mi.UnionType):
            structs = {}
            other = []
            tag_field = None
            for subtype in t.types:
                real_type = subtype
                while isinstance(real_type, mi.Metadata):
                    real_type = real_type.type
                if isinstance(real_type, mi.StructType) and not real_type.array_like:
                    tag_field = real_type.tag_field
                    structs[real_type.tag] = real_type
                else:
                    other.append(subtype)

            options = [self.to_schema(a) for a in other]

            if len(structs) >= 2:
                mapping = {
                    k: self.ref_template.format(name=self.name_map[v.cls])
                    for k, v in structs.items()
                }
                struct_schema = {
                    "anyOf": [self.to_schema(v) for v in structs.values()],
                    "discriminator": {"propertyName": tag_field, "mapping": mapping},
                }
                if options:
                    options.append(struct_schema)
                    schema["anyOf"] = options
                else:
                    schema.update(struct_schema)
            elif len(structs) == 1:
                _, subtype = structs.popitem()
                options.append(self.to_schema(subtype))
                schema["anyOf"] = options
            else:
                schema["anyOf"] = options
        elif isinstance(t, mi.LiteralType):
            schema["enum"] = sorted(t.values)
        elif isinstance(t, mi.EnumType):
            schema.setdefault("title", t.cls.__name__)
            if doc := _get_doc(t):
                schema.setdefault("description", doc)
            schema["enum"] = sorted(e.value for e in t.cls)
        elif isinstance(t, mi.StructType):
            schema.setdefault("title", _get_class_name(t.cls))
            if doc := _get_doc(t):
                schema.setdefault("description", doc)
            required = []
            names = []
            fields = []

            if t.tag_field is not None:
                required.append(t.tag_field)
                names.append(t.tag_field)
                fields.append({"enum": [t.tag]})

            for field in t.fields:
                field_schema = self.to_schema(field.type)
                if field.required:
                    required.append(field.encode_name)
                elif field.default is not mi.NODEFAULT:
                    field_schema["default"] = to_builtins(field.default, str_keys=True)
                elif field.default_factory in (list, dict, set, bytearray):
                    field_schema["default"] = field.default_factory()
                names.append(field.encode_name)
                fields.append(field_schema)

            if t.array_like:
                n_trailing_defaults = 0
                for n_trailing_defaults, f in enumerate(reversed(t.fields)):
                    if f.required:
                        break
                schema["type"] = "array"
                schema["prefixItems"] = fields
                schema["minItems"] = len(fields) - n_trailing_defaults
                if t.forbid_unknown_fields:
                    schema["maxItems"] = len(fields)
            else:
                schema["type"] = "object"
                schema["properties"] = dict(zip(names, fields))
                schema["required"] = required
                if t.forbid_unknown_fields:
                    schema["additionalProperties"] = False
        elif isinstance(t, (mi.TypedDictType, mi.DataclassType, mi.NamedTupleType)):
            schema.setdefault("title", _get_class_name(t.cls))
            if doc := _get_doc(t):
                schema.setdefault("description", doc)
            names = []
            fields = []
            required = []
            for field in t.fields:
                field_schema = self.to_schema(field.type)
                if field.required:
                    required.append(field.encode_name)
                elif field.default is not mi.NODEFAULT:
                    field_schema["default"] = to_builtins(field.default, str_keys=True)
                names.append(field.encode_name)
                fields.append(field_schema)
            if isinstance(t, mi.NamedTupleType):
                schema["type"] = "array"
                schema["prefixItems"] = fields
                schema["minItems"] = len(required)
                schema["maxItems"] = len(fields)
            else:
                schema["type"] = "object"
                schema["properties"] = dict(zip(names, fields))
                schema["required"] = required
        elif isinstance(t, mi.ExtType):
            raise TypeError("json-schema doesn't support msgpack Ext types")
        elif isinstance(t, mi.CustomType):
            if self.schema_hook:
                try:
                    schema = mi._merge_json(self.schema_hook(t.cls), schema)
                except NotImplementedError:
                    pass
            if not schema:
                raise TypeError(
                    "Generating JSON schema for custom types requires either:\n"
                    "- specifying a `schema_hook`\n"
                    "- annotating the type with `Meta(extra_json_schema=...)`\n"
                    "\n"
                    f"type {t.cls!r} is not supported"
                )
        else:
            # This should be unreachable
            raise TypeError(f"json-schema doesn't support type {t!r}")

        return schema
