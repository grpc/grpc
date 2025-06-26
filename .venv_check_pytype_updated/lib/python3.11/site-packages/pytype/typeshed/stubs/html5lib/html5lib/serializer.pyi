from _typeshed import Incomplete
from typing import Any, overload

def htmlentityreplace_errors(exc: Exception) -> tuple[str | bytes, int]: ...
@overload
def serialize(input, tree: str = "etree", encoding: None = None, **serializer_opts) -> str: ...
@overload
def serialize(input, tree: str, encoding: str, **serializer_opts) -> bytes: ...
@overload
def serialize(input, *, encoding: str, **serializer_opts) -> bytes: ...

class HTMLSerializer:
    quote_attr_values: str
    quote_char: str
    use_best_quote_char: bool
    omit_optional_tags: bool
    minimize_boolean_attributes: bool
    use_trailing_solidus: bool
    space_before_trailing_solidus: bool
    escape_lt_in_attrs: bool
    escape_rcdata: bool
    resolve_entities: bool
    alphabetical_attributes: bool
    inject_meta_charset: bool
    strip_whitespace: bool
    sanitize: bool
    options: Any
    errors: Any
    strict: bool
    def __init__(self, **kwargs) -> None: ...
    def encode(self, string): ...
    def encodeStrict(self, string): ...
    encoding: Any
    def serialize(self, treewalker, encoding: Incomplete | None = None) -> None: ...
    def render(self, treewalker, encoding: Incomplete | None = None): ...
    def serializeError(self, data: str = "XXX ERROR MESSAGE NEEDED") -> None: ...

class SerializeError(Exception): ...
