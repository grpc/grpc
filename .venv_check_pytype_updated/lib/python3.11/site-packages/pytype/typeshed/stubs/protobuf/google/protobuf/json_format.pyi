from typing import Any, TypeVar

from google.protobuf.descriptor_pool import DescriptorPool
from google.protobuf.message import Message

_MessageT = TypeVar("_MessageT", bound=Message)

class Error(Exception): ...
class ParseError(Error): ...
class SerializeToJsonError(Error): ...

def MessageToJson(
    message: Message,
    including_default_value_fields: bool = ...,
    preserving_proto_field_name: bool = ...,
    indent: int | None = ...,
    sort_keys: bool = ...,
    use_integers_for_enums: bool = ...,
    descriptor_pool: DescriptorPool | None = ...,
    float_precision: int | None = ...,
    ensure_ascii: bool = ...,
) -> str: ...
def MessageToDict(
    message: Message,
    including_default_value_fields: bool = ...,
    preserving_proto_field_name: bool = ...,
    use_integers_for_enums: bool = ...,
    descriptor_pool: DescriptorPool | None = ...,
    float_precision: int | None = ...,
) -> dict[str, Any]: ...
def Parse(
    text: bytes | str,
    message: _MessageT,
    ignore_unknown_fields: bool = ...,
    descriptor_pool: DescriptorPool | None = ...,
    max_recursion_depth: int = ...,
) -> _MessageT: ...
def ParseDict(
    js_dict: Any,
    message: _MessageT,
    ignore_unknown_fields: bool = ...,
    descriptor_pool: DescriptorPool | None = ...,
    max_recursion_depth: int = ...,
) -> _MessageT: ...
