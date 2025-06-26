# type: ignore
import collections
import sys
import typing

from typing import _AnnotatedAlias  # noqa: F401

try:
    from typing_extensions import get_type_hints as _get_type_hints
except Exception:
    from typing import get_type_hints as _get_type_hints

try:
    from typing_extensions import NotRequired, Required
except Exception:
    try:
        from typing import NotRequired, Required
    except Exception:
        Required = NotRequired = None


def get_type_hints(obj):
    return _get_type_hints(obj, include_extras=True)


# The `is_class` argument was new in 3.11, but was backported to 3.9 and 3.10.
# It's _likely_ to be available for 3.9/3.10, but may not be. Easiest way to
# check is to try it and see. This check can be removed when we drop support
# for Python 3.10.
try:
    typing.ForwardRef("Foo", is_class=True)
except TypeError:

    def _forward_ref(value):
        return typing.ForwardRef(value, is_argument=False)

else:

    def _forward_ref(value):
        return typing.ForwardRef(value, is_argument=False, is_class=True)


# Python 3.13 adds a new mandatory type_params kwarg to _eval_type
if sys.version_info >= (3, 13):

    def _eval_type(t, globalns, localns):
        return typing._eval_type(t, globalns, localns, ())
elif sys.version_info < (3, 10):

    def _eval_type(t, globalns, localns):
        try:
            return typing._eval_type(t, globalns, localns)
        except TypeError as e:
            try:
                from eval_type_backport import eval_type_backport
            except ImportError:
                raise TypeError(
                    f"Unable to evaluate type annotation {t.__forward_arg__!r}. If you are making use "
                    "of the new typing syntax (unions using `|` since Python 3.10 or builtins subscripting "
                    "since Python 3.9), you should either replace the use of new syntax with the existing "
                    "`typing` constructs or install the `eval_type_backport` package."
                ) from e

            return eval_type_backport(
                t,
                globalns,
                localns,
                try_default=False,
            )
else:
    _eval_type = typing._eval_type


def _apply_params(obj, mapping):
    if isinstance(obj, typing.TypeVar):
        return mapping.get(obj, obj)

    try:
        parameters = tuple(obj.__parameters__)
    except Exception:
        # Not parameterized or __parameters__ is invalid, ignore
        return obj

    if not parameters:
        # Not parametrized
        return obj

    # Parametrized
    args = tuple(mapping.get(p, p) for p in parameters)
    return obj[args]


def _get_class_mro_and_typevar_mappings(obj):
    mapping = {}

    if isinstance(obj, type):
        cls = obj
    else:
        cls = obj.__origin__

    def inner(c, scope):
        if isinstance(c, type):
            cls = c
            new_scope = {}
        else:
            cls = getattr(c, "__origin__", None)
            if cls in (None, object, typing.Generic) or cls in mapping:
                return
            params = cls.__parameters__
            args = tuple(_apply_params(a, scope) for a in c.__args__)
            assert len(params) == len(args)
            mapping[cls] = new_scope = dict(zip(params, args))

        if issubclass(cls, typing.Generic):
            bases = getattr(cls, "__orig_bases__", cls.__bases__)
            for b in bases:
                inner(b, new_scope)

    inner(obj, {})
    return cls.__mro__, mapping


def get_class_annotations(obj):
    """Get the annotations for a class.

    This is similar to ``typing.get_type_hints``, except:

    - We maintain it
    - It leaves extras like ``Annotated``/``ClassVar`` alone
    - It resolves any parametrized generics in the class mro. The returned
      mapping may still include ``TypeVar`` values, but those should be treated
      as their unparametrized variants (i.e. equal to ``Any`` for the common case).

    Note that this function doesn't check that Generic types are being used
    properly - invalid uses of `Generic` may slip through without complaint.

    The assumption here is that the user is making use of a static analysis
    tool like ``mypy``/``pyright`` already, which would catch misuse of these
    APIs.
    """
    hints = {}
    mro, typevar_mappings = _get_class_mro_and_typevar_mappings(obj)

    for cls in mro:
        if cls in (typing.Generic, object):
            continue

        mapping = typevar_mappings.get(cls)
        cls_locals = dict(vars(cls))
        cls_globals = getattr(sys.modules.get(cls.__module__, None), "__dict__", {})

        ann = cls.__dict__.get("__annotations__", {})
        for name, value in ann.items():
            if name in hints:
                continue
            if value is None:
                value = type(None)
            elif isinstance(value, str):
                value = _forward_ref(value)
            value = _eval_type(value, cls_locals, cls_globals)
            if mapping is not None:
                value = _apply_params(value, mapping)
            hints[name] = value
    return hints


# A mapping from a type annotation (or annotation __origin__) to the concrete
# python type that msgspec will use when decoding. THIS IS PRIVATE FOR A
# REASON. DON'T MUCK WITH THIS.
_CONCRETE_TYPES = {
    list: list,
    tuple: tuple,
    set: set,
    frozenset: frozenset,
    dict: dict,
    typing.List: list,
    typing.Tuple: tuple,
    typing.Set: set,
    typing.FrozenSet: frozenset,
    typing.Dict: dict,
    typing.Collection: list,
    typing.MutableSequence: list,
    typing.Sequence: list,
    typing.MutableMapping: dict,
    typing.Mapping: dict,
    typing.MutableSet: set,
    typing.AbstractSet: set,
    collections.abc.Collection: list,
    collections.abc.MutableSequence: list,
    collections.abc.Sequence: list,
    collections.abc.MutableSet: set,
    collections.abc.Set: set,
    collections.abc.MutableMapping: dict,
    collections.abc.Mapping: dict,
}


def get_typeddict_info(obj):
    if isinstance(obj, type):
        cls = obj
    else:
        cls = obj.__origin__

    raw_hints = get_class_annotations(obj)

    if hasattr(cls, "__required_keys__"):
        required = set(cls.__required_keys__)
    elif cls.__total__:
        required = set(raw_hints)
    else:
        required = set()

    # Both `typing.TypedDict` and `typing_extensions.TypedDict` have a bug
    # where `Required`/`NotRequired` aren't properly detected at runtime when
    # `__future__.annotations` is enabled, meaning the `__required_keys__`
    # isn't correct. This code block works around this issue by amending the
    # set of required keys as needed, while also stripping off any
    # `Required`/`NotRequired` wrappers.
    hints = {}
    for k, v in raw_hints.items():
        origin = getattr(v, "__origin__", False)
        if origin is Required:
            required.add(k)
            hints[k] = v.__args__[0]
        elif origin is NotRequired:
            required.discard(k)
            hints[k] = v.__args__[0]
        else:
            hints[k] = v
    return hints, required


def get_dataclass_info(obj):
    if isinstance(obj, type):
        cls = obj
    else:
        cls = obj.__origin__
    hints = get_class_annotations(obj)
    required = []
    optional = []
    defaults = []

    if hasattr(cls, "__dataclass_fields__"):
        from dataclasses import _FIELD, _FIELD_INITVAR, MISSING

        for field in cls.__dataclass_fields__.values():
            if field._field_type is not _FIELD:
                if field._field_type is _FIELD_INITVAR:
                    raise TypeError(
                        "dataclasses with `InitVar` fields are not supported"
                    )
                continue
            name = field.name
            typ = hints[name]
            if field.default is not MISSING:
                defaults.append(field.default)
                optional.append((name, typ, False))
            elif field.default_factory is not MISSING:
                defaults.append(field.default_factory)
                optional.append((name, typ, True))
            else:
                required.append((name, typ, False))

        required.extend(optional)

        pre_init = None
        post_init = getattr(cls, "__post_init__", None)
    else:
        from attrs import NOTHING, Factory

        fields_with_validators = []

        for field in cls.__attrs_attrs__:
            name = field.name
            typ = hints[name]
            default = field.default
            if default is not NOTHING:
                if isinstance(default, Factory):
                    if default.takes_self:
                        raise NotImplementedError(
                            "Support for default factories with `takes_self=True` "
                            "is not implemented. File a GitHub issue if you need "
                            "this feature!"
                        )
                    defaults.append(default.factory)
                    optional.append((name, typ, True))
                else:
                    defaults.append(default)
                    optional.append((name, typ, False))
            else:
                required.append((name, typ, False))

            if field.validator is not None:
                fields_with_validators.append(field)

        required.extend(optional)

        pre_init = getattr(cls, "__attrs_pre_init__", None)
        post_init = getattr(cls, "__attrs_post_init__", None)

        if fields_with_validators:
            post_init = _wrap_attrs_validators(fields_with_validators, post_init)

    return cls, tuple(required), tuple(defaults), pre_init, post_init


def _wrap_attrs_validators(fields, post_init):
    def inner(obj):
        for field in fields:
            field.validator(obj, field, getattr(obj, field.name))
        if post_init is not None:
            post_init(obj)

    return inner


def rebuild(cls, kwargs):
    """Used to unpickle Structs with keyword-only fields"""
    return cls(**kwargs)
