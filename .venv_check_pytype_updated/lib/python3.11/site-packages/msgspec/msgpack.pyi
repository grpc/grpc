from typing import (
    Any,
    Callable,
    Generic,
    Literal,
    Optional,
    Type,
    TypeVar,
    Union,
    overload,
)

from typing_extensions import Buffer


T = TypeVar("T")

enc_hook_sig = Optional[Callable[[Any], Any]]
ext_hook_sig = Optional[Callable[[int, memoryview], Any]]
dec_hook_sig = Optional[Callable[[type, Any], Any]]

class Ext:
    code: int
    data: Union[bytes, bytearray, memoryview]
    def __init__(
        self, code: int, data: Union[bytes, bytearray, memoryview]
    ) -> None: ...

class Decoder(Generic[T]):
    type: Type[T]
    strict: bool
    dec_hook: dec_hook_sig
    ext_hook: ext_hook_sig
    @overload
    def __init__(
        self: Decoder[Any],
        *,
        strict: bool = True,
        dec_hook: dec_hook_sig = None,
        ext_hook: ext_hook_sig = None,
    ) -> None: ...
    @overload
    def __init__(
        self: Decoder[T],
        type: Type[T] = ...,
        *,
        strict: bool = True,
        dec_hook: dec_hook_sig = None,
        ext_hook: ext_hook_sig = None,
    ) -> None: ...
    @overload
    def __init__(
        self: Decoder[Any],
        type: Any = ...,
        *,
        strict: bool = True,
        dec_hook: dec_hook_sig = None,
        ext_hook: ext_hook_sig = None,
    ) -> None: ...
    def decode(self, buf: Buffer, /) -> T: ...

class Encoder:
    enc_hook: enc_hook_sig
    decimal_format: Literal["string", "number"]
    uuid_format: Literal["canonical", "hex", "bytes"]
    order: Literal[None, "deterministic", "sorted"]
    def __init__(
        self,
        *,
        enc_hook: enc_hook_sig = None,
        decimal_format: Literal["string", "number"] = "string",
        uuid_format: Literal["canonical", "hex", "bytes"] = "canonical",
        order: Literal[None, "deterministic", "sorted"] = None,
    ): ...
    def encode(self, obj: Any, /) -> bytes: ...
    def encode_into(
        self, obj: Any, buffer: bytearray, offset: Optional[int] = 0, /
    ) -> None: ...

@overload
def decode(
    buf: Buffer,
    /,
    *,
    strict: bool = True,
    dec_hook: dec_hook_sig = None,
    ext_hook: ext_hook_sig = None,
) -> Any: ...
@overload
def decode(
    buf: Buffer,
    /,
    *,
    type: Type[T] = ...,
    strict: bool = True,
    dec_hook: dec_hook_sig = None,
    ext_hook: ext_hook_sig = None,
) -> T: ...
@overload
def decode(
    buf: Buffer,
    /,
    *,
    type: Any = ...,
    strict: bool = True,
    dec_hook: dec_hook_sig = None,
    ext_hook: ext_hook_sig = None,
) -> Any: ...
def encode(obj: Any, /, *, enc_hook: enc_hook_sig = None, order: Literal[None, "deterministic", "sorted"] = None) -> bytes: ...
