# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import (
    Any,
    ClassVar,
    ForwardRef,
    get_args,
    get_origin,
    Iterable,
    Literal,
    Mapping,
    MutableMapping,
    MutableSequence,
    Tuple,
    TypeVar,
    Union,
)


def is_value_of_type(  # noqa: C901 "too complex"
    # pyre-fixme[2]: Parameter annotation cannot be `Any`.
    value: Any,
    # pyre-fixme[2]: Parameter annotation cannot be `Any`.
    expected_type: Any,
    invariant_check: bool = False,
) -> bool:
    """
    This method attempts to verify a given value is of a given type. If the type is
    not supported, it returns True but throws an exception in tests.

    It is similar to typeguard / enforce pypi modules, but neither of those have
    permissive options for types they do not support.

    Supported types for now:
    - List/Set/Iterable
    - Dict/Mapping
    - base types (str, int, etc)
    - Literal
    - Unions
    - Tuples
    - Concrete Classes
    - ClassVar

    Not supported:
    - Callables, which will likely not be used in XHP anyways
    - Generics, Type Vars (treated as Any)
    - Generators
    - Forward Refs -- use `typing.get_type_hints` to resolve these
    - Type[...]
    """
    if expected_type is ClassVar or get_origin(expected_type) is ClassVar:
        classvar_args = get_args(expected_type)
        expected_type = (classvar_args[0] or Any) if classvar_args else Any

    if type(expected_type) is TypeVar:
        # treat this the same as Any
        # TODO: evaluate bounds
        return True

    expected_origin_type = get_origin(expected_type) or expected_type

    if expected_origin_type == Any:
        return True

    elif expected_type is Union or get_origin(expected_type) is Union:
        return any(
            is_value_of_type(value, subtype) for subtype in expected_type.__args__
        )

    elif isinstance(expected_origin_type, type(Literal)):
        literal_values = get_args(expected_type)
        return any(value == literal for literal in literal_values)

    elif isinstance(expected_origin_type, ForwardRef):
        # not much we can do here for now, lets just return :(
        return True

    # Handle `Tuple[A, B, C]`.
    # We don't want to include Tuple subclasses, like NamedTuple, because they're
    # unlikely to behave similarly.
    elif expected_origin_type in [Tuple, tuple]:  # py36 uses Tuple, py37+ uses tuple
        if not isinstance(value, tuple):
            return False

        type_args = get_args(expected_type)
        if len(type_args) == 0:
            # `Tuple` (no subscript) is implicitly `Tuple[Any, ...]`
            return True

        if len(value) != len(type_args):
            return False
        # TODO: Handle `Tuple[T, ...]` like `Iterable[T]`
        for subvalue, subtype in zip(value, type_args):
            if not is_value_of_type(subvalue, subtype):
                return False
            return True

    elif issubclass(expected_origin_type, Mapping):
        # We're expecting *some* kind of Mapping, but we also want to make sure it's
        # the correct Mapping subtype. That means we want {a: b, c: d} to match Mapping,
        # MutableMapping, and Dict, but we don't want MappingProxyType({a: b, c: d}) to
        # match MutableMapping or Dict.
        if not issubclass(type(value), expected_origin_type):
            return False

        type_args = get_args(expected_type)
        if len(type_args) == 0:
            # `Mapping` (no subscript) is implicitly `Mapping[Any, Any]`.
            return True

        invariant_check = issubclass(expected_origin_type, MutableMapping)

        for subkey, subvalue in value.items():
            if not is_value_of_type(
                subkey,
                type_args[0],
                # key type is always invariant
                invariant_check=True,
            ):
                return False
            if not is_value_of_type(
                subvalue, type_args[1], invariant_check=invariant_check
            ):
                return False
        return True

    # While this does technically work fine for str and bytes (they are iterables), it's
    # better to use the default isinstance behavior for them.
    #
    # Similarly, tuple subclasses tend to have pretty different behavior, and we should
    # fall back to the default check.
    elif issubclass(expected_origin_type, Iterable) and not issubclass(
        expected_origin_type,
        (str, bytes, tuple),
    ):
        # We know this thing is *some* kind of Iterable, but we want to
        # allow subclasses. That means we want [1,2,3] to match both
        # List[int] and Iterable[int], but we do NOT want that
        # to match Set[int].
        if not issubclass(type(value), expected_origin_type):
            return False

        type_args = get_args(expected_type)
        if len(type_args) == 0:
            # `Iterable` (no subscript) is implicitly `Iterable[Any]`.
            return True

        # We invariant check if its a mutable sequence
        invariant_check = issubclass(expected_origin_type, MutableSequence)
        return all(
            is_value_of_type(subvalue, type_args[0], invariant_check=invariant_check)
            for subvalue in value
        )

    try:
        if not invariant_check:
            if expected_type is float:
                return isinstance(value, (int, float))
            else:
                return isinstance(value, expected_type)
        return type(value) is expected_type
    except Exception as e:
        raise NotImplementedError(
            f"the value {value!r} was compared to type {expected_type!r} "
            + f"but support for that has not been implemented yet! Exception: {e!r}"
        )
