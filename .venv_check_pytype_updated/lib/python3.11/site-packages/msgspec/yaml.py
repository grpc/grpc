from __future__ import annotations

import datetime as _datetime
from typing import TYPE_CHECKING, overload, TypeVar, Any

from . import (
    DecodeError as _DecodeError,
    convert as _convert,
    to_builtins as _to_builtins,
)

if TYPE_CHECKING:
    from typing import Callable, Optional, Type, Union, Literal
    from typing_extensions import Buffer


__all__ = ("encode", "decode")


def __dir__():
    return __all__


def _import_pyyaml(name):
    try:
        import yaml  # type: ignore
    except ImportError:
        raise ImportError(
            f"`msgspec.yaml.{name}` requires PyYAML be installed.\n\n"
            "Please either `pip` or `conda` install it as follows:\n\n"
            "  $ python -m pip install pyyaml  # using pip\n"
            "  $ conda install pyyaml          # or using conda"
        ) from None
    else:
        return yaml


def encode(
    obj: Any,
    *,
    enc_hook: Optional[Callable[[Any], Any]] = None,
    order: Literal[None, "deterministic", "sorted"] = None,
) -> bytes:
    """Serialize an object as YAML.

    Parameters
    ----------
    obj : Any
        The object to serialize.
    enc_hook : callable, optional
        A callable to call for objects that aren't supported msgspec types.
        Takes the unsupported object and should return a supported object, or
        raise a ``NotImplementedError`` if unsupported.
    order : {None, 'deterministic', 'sorted'}, optional
        The ordering to use when encoding unordered compound types.

        - ``None``: All objects are encoded in the most efficient manner
          matching their in-memory representations. The default.
        - `'deterministic'`: Unordered collections (sets, dicts) are sorted to
          ensure a consistent output between runs. Useful when
          comparison/hashing of the encoded binary output is necessary.
        - `'sorted'`: Like `'deterministic'`, but *all* object-like types
          (structs, dataclasses, ...) are also sorted by field name before
          encoding. This is slower than `'deterministic'`, but may produce more
          human-readable output.

    Returns
    -------
    data : bytes
        The serialized object.

    Notes
    -----
    This function requires that the third-party `PyYAML library
    <https://pyyaml.org/>`_ is installed.

    See Also
    --------
    decode
    """
    yaml = _import_pyyaml("encode")
    # Use the C extension if available
    Dumper = getattr(yaml, "CSafeDumper", yaml.SafeDumper)

    return yaml.dump_all(
        [
            _to_builtins(
                obj,
                builtin_types=(_datetime.datetime, _datetime.date),
                enc_hook=enc_hook,
                order=order,
            )
        ],
        encoding="utf-8",
        Dumper=Dumper,
        allow_unicode=True,
        sort_keys=False,
    )


T = TypeVar("T")


@overload
def decode(
    buf: Union[Buffer, str],
    *,
    strict: bool = True,
    dec_hook: Optional[Callable[[type, Any], Any]] = None,
) -> Any:
    pass


@overload
def decode(
    buf: Union[bytes, str],
    *,
    type: Type[T] = ...,
    strict: bool = True,
    dec_hook: Optional[Callable[[type, Any], Any]] = None,
) -> T:
    pass


@overload
def decode(
    buf: Union[bytes, str],
    *,
    type: Any = ...,
    strict: bool = True,
    dec_hook: Optional[Callable[[type, Any], Any]] = None,
) -> Any:
    pass


def decode(buf, *, type=Any, strict=True, dec_hook=None):
    """Deserialize an object from YAML.

    Parameters
    ----------
    buf : bytes-like or str
        The message to decode.
    type : type, optional
        A Python type (in type annotation form) to decode the object as. If
        provided, the message will be type checked and decoded as the specified
        type. Defaults to `Any`, in which case the message will be decoded
        using the default YAML types.
    strict : bool, optional
        Whether type coercion rules should be strict. Setting to False enables
        a wider set of coercion rules from string to non-string types for all
        values. Default is True.
    dec_hook : callable, optional
        An optional callback for handling decoding custom types. Should have
        the signature ``dec_hook(type: Type, obj: Any) -> Any``, where ``type``
        is the expected message type, and ``obj`` is the decoded representation
        composed of only basic YAML types. This hook should transform ``obj``
        into type ``type``, or raise a ``NotImplementedError`` if unsupported.

    Returns
    -------
    obj : Any
        The deserialized object.

    Notes
    -----
    This function requires that the third-party `PyYAML library
    <https://pyyaml.org/>`_ is installed.

    See Also
    --------
    encode
    """
    yaml = _import_pyyaml("decode")
    # Use the C extension if available
    Loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)
    if not isinstance(buf, (str, bytes)):
        # call `memoryview` first, since `bytes(1)` is actually valid
        buf = bytes(memoryview(buf))
    try:
        obj = yaml.load(buf, Loader)
    except yaml.YAMLError as exc:
        raise _DecodeError(str(exc)) from None

    if type is Any:
        return obj
    return _convert(
        obj,
        type,
        builtin_types=(_datetime.datetime, _datetime.date),
        strict=strict,
        dec_hook=dec_hook,
    )
