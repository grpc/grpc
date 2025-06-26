from collections.abc import Callable
from typing import Any
from typing_extensions import TypeAlias

from google.protobuf.descriptor import Descriptor, FieldDescriptor
from google.protobuf.message import Message

_Decoder: TypeAlias = Callable[[str, int, int, Message, dict[FieldDescriptor, Any]], int]
_NewDefault: TypeAlias = Callable[[Message], Message]

def ReadTag(buffer, pos): ...

Int32Decoder: _Decoder
Int64Decoder: _Decoder
UInt32Decoder: _Decoder
UInt64Decoder: _Decoder
SInt32Decoder: _Decoder
SInt64Decoder: _Decoder
Fixed32Decoder: _Decoder
Fixed64Decoder: _Decoder
SFixed32Decoder: _Decoder
SFixed64Decoder: _Decoder
FloatDecoder: _Decoder
DoubleDecoder: _Decoder
BoolDecoder: _Decoder

def EnumDecoder(
    field_number: int,
    is_repeated: bool,
    is_packed: bool,
    key: FieldDescriptor,
    new_default: _NewDefault,
    clear_if_default: bool = ...,
) -> _Decoder: ...
def StringDecoder(
    field_number: int,
    is_repeated: bool,
    is_packed: bool,
    key: FieldDescriptor,
    new_default: _NewDefault,
    clear_if_default: bool = ...,
) -> _Decoder: ...
def BytesDecoder(
    field_number: int,
    is_repeated: bool,
    is_packed: bool,
    key: FieldDescriptor,
    new_default: _NewDefault,
    clear_if_default: bool = ...,
) -> _Decoder: ...
def GroupDecoder(
    field_number: int, is_repeated: bool, is_packed: bool, key: FieldDescriptor, new_default: _NewDefault
) -> _Decoder: ...
def MessageDecoder(
    field_number: int, is_repeated: bool, is_packed: bool, key: FieldDescriptor, new_default: _NewDefault
) -> _Decoder: ...

MESSAGE_SET_ITEM_TAG: bytes

def MessageSetItemDecoder(descriptor: Descriptor) -> _Decoder: ...
def MapDecoder(field_descriptor, new_default, is_message_map) -> _Decoder: ...

SkipField: Any
