from _typeshed import Incomplete
from collections.abc import Iterable, Iterator
from logging import Logger
from typing import Any, Literal

def chardet_dammit(s): ...

xml_encoding: str
html_meta: str
encoding_res: Any

class EntitySubstitution:
    CHARACTER_TO_HTML_ENTITY: Any
    HTML_ENTITY_TO_CHARACTER: Any
    CHARACTER_TO_HTML_ENTITY_RE: Any
    CHARACTER_TO_XML_ENTITY: Any
    BARE_AMPERSAND_OR_BRACKET: Any
    AMPERSAND_OR_BRACKET: Any
    @classmethod
    def quoted_attribute_value(cls, value): ...
    @classmethod
    def substitute_xml(cls, value, make_quoted_attribute: bool = False): ...
    @classmethod
    def substitute_xml_containing_entities(cls, value, make_quoted_attribute: bool = False): ...
    @classmethod
    def substitute_html(cls, s): ...

class EncodingDetector:
    known_definite_encodings: list[str]
    user_encodings: list[str]
    exclude_encodings: set[str]
    chardet_encoding: Incomplete | None
    is_html: bool
    declared_encoding: str | None
    markup: Any
    sniffed_encoding: str | None
    def __init__(
        self,
        markup,
        known_definite_encodings: Iterable[str] | None = None,
        is_html: bool = False,
        exclude_encodings: list[str] | None = None,
        user_encodings: list[str] | None = None,
        override_encodings: list[str] | None = None,
    ) -> None: ...
    @property
    def encodings(self) -> Iterator[str]: ...
    @classmethod
    def strip_byte_order_mark(cls, data): ...
    @classmethod
    def find_declared_encoding(cls, markup, is_html: bool = False, search_entire_document: bool = False) -> str | None: ...

class UnicodeDammit:
    CHARSET_ALIASES: dict[str, str]
    ENCODINGS_WITH_SMART_QUOTES: list[str]
    smart_quotes_to: Literal["ascii", "xml", "html"] | None
    tried_encodings: list[tuple[str, str]]
    contains_replacement_characters: bool
    is_html: bool
    log: Logger
    detector: EncodingDetector
    markup: Any
    unicode_markup: str
    original_encoding: Incomplete | None
    def __init__(
        self,
        markup,
        known_definite_encodings: list[str] | None = [],
        smart_quotes_to: Literal["ascii", "xml", "html"] | None = None,
        is_html: bool = False,
        exclude_encodings: list[str] | None = [],
        user_encodings: list[str] | None = None,
        override_encodings: list[str] | None = None,
    ) -> None: ...
    @property
    def declared_html_encoding(self) -> str | None: ...
    def find_codec(self, charset: str) -> str | None: ...
    MS_CHARS: dict[bytes, str | tuple[str, ...]]
    MS_CHARS_TO_ASCII: dict[bytes, str]
    WINDOWS_1252_TO_UTF8: dict[int, bytes]
    MULTIBYTE_MARKERS_AND_SIZES: list[tuple[int, int, int]]
    FIRST_MULTIBYTE_MARKER: int
    LAST_MULTIBYTE_MARKER: int
    @classmethod
    def detwingle(cls, in_bytes: bytes, main_encoding: str = "utf8", embedded_encoding: str = "windows-1252") -> bytes: ...
