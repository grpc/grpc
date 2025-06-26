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


def _import_tomllib():
    try:
        import tomllib  # type: ignore

        return tomllib
    except ImportError:
        pass

    try:
        import tomli  # type: ignore

        return tomli
    except ImportError:
        raise ImportError(
            "`msgspec.toml.decode` requires `tomli` be installed.\n\n"
            "Please either `pip` or `conda` install it as follows:\n\n"
            "  $ python -m pip install tomli   # using pip\n"
            "  $ conda install tomli           # or using conda"
        ) from None


def _import_tomli_w():
    try:
        import tomli_w  # type: ignore

        return tomli_w
    except ImportError:
        raise ImportError(
            "`msgspec.toml.encode` requires `tomli_w` be installed.\n\n"
            "Please either `pip` or `conda` install it as follows:\n\n"
            "  $ python -m pip install tomli_w   # using pip\n"
            "  $ conda install tomli_w           # or using conda"
        ) from None


def encode(
    obj: Any,
    *,
    enc_hook: Optional[Callable[[Any], Any]] = None,
    order: Literal[None, "deterministic", "sorted"] = None,
) -> bytes:
    """Serialize an object as TOML.

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

    See Also
    --------
    decode
    """
    toml = _import_tomli_w()
    msg = _to_builtins(
        obj,
        builtin_types=(_datetime.datetime, _datetime.date, _datetime.time),
        str_keys=True,
        enc_hook=enc_hook,
        order=order,
    )
    return toml.dumps(msg).encode("utf-8")


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
    buf: Union[Buffer, str],
    *,
    type: Type[T] = ...,
    strict: bool = True,
    dec_hook: Optional[Callable[[type, Any], Any]] = None,
) -> T:
    pass


@overload
def decode(
    buf: Union[Buffer, str],
    *,
    type: Any = ...,
    strict: bool = True,
    dec_hook: Optional[Callable[[type, Any], Any]] = None,
) -> Any:
    pass


def decode(buf, *, type=Any, strict=True, dec_hook=None):
    """Deserialize an object from TOML.

    Parameters
    ----------
    buf : bytes-like or str
        The message to decode.
    type : type, optional
        A Python type (in type annotation form) to decode the object as. If
        provided, the message will be type checked and decoded as the specified
        type. Defaults to `Any`, in which case the message will be decoded
        using the default TOML types.
    strict : bool, optional
        Whether type coercion rules should be strict. Setting to False enables
        a wider set of coercion rules from string to non-string types for all
        values. Default is True.
    dec_hook : callable, optional
        An optional callback for handling decoding custom types. Should have
        the signature ``dec_hook(type: Type, obj: Any) -> Any``, where ``type``
        is the expected message type, and ``obj`` is the decoded representation
        composed of only basic TOML types. This hook should transform ``obj``
        into type ``type``, or raise a ``NotImplementedError`` if unsupported.

    Returns
    -------
    obj : Any
        The deserialized object.

    See Also
    --------
    encode
    """
    toml = _import_tomllib()
    if isinstance(buf, str):
        str_buf = buf
    elif isinstance(buf, (bytes, bytearray)):
        str_buf = buf.decode("utf-8")
    else:
        # call `memoryview` first, since `bytes(1)` is actually valid
        str_buf = bytes(memoryview(buf)).decode("utf-8")
    try:
        obj = toml.loads(str_buf)
    except toml.TOMLDecodeError as exc:
        raise _DecodeError(str(exc)) from None

    if type is Any:
        return obj
    return _convert(
        obj,
        type,
        builtin_types=(_datetime.datetime, _datetime.date, _datetime.time),
        str_keys=True,
        strict=strict,
        dec_hook=dec_hook,
    )
