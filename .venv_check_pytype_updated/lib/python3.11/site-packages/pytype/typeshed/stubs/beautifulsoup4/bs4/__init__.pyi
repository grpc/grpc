from _typeshed import Incomplete, SupportsRead
from collections.abc import Iterator, Sequence
from typing import Any
from typing_extensions import Self

from .builder import ParserRejectedMarkup as ParserRejectedMarkup, TreeBuilder, XMLParsedAsHTMLWarning as XMLParsedAsHTMLWarning
from .element import (
    CData as CData,
    Comment as Comment,
    Declaration as Declaration,
    Doctype as Doctype,
    NavigableString as NavigableString,
    PageElement as PageElement,
    ProcessingInstruction as ProcessingInstruction,
    ResultSet as ResultSet,
    Script as Script,
    SoupStrainer as SoupStrainer,
    Stylesheet as Stylesheet,
    Tag as Tag,
    TemplateString as TemplateString,
)
from .formatter import Formatter

class GuessedAtParserWarning(UserWarning): ...
class MarkupResemblesLocatorWarning(UserWarning): ...

class BeautifulSoup(Tag):
    ROOT_TAG_NAME: str
    DEFAULT_BUILDER_FEATURES: list[str]
    ASCII_SPACES: str
    NO_PARSER_SPECIFIED_WARNING: str
    element_classes: Any
    builder: TreeBuilder
    is_xml: bool
    known_xml: bool | None
    parse_only: SoupStrainer | None
    markup: str
    def __init__(
        self,
        markup: str | bytes | SupportsRead[str] | SupportsRead[bytes] = "",
        features: str | Sequence[str] | None = None,
        builder: TreeBuilder | type[TreeBuilder] | None = None,
        parse_only: SoupStrainer | None = None,
        from_encoding: str | None = None,
        exclude_encodings: Sequence[str] | None = None,
        element_classes: dict[type[PageElement], type[Any]] | None = None,
        **kwargs,
    ) -> None: ...
    def __copy__(self) -> Self: ...
    hidden: bool
    current_data: Any
    currentTag: Any
    tagStack: Any
    open_tag_counter: Any
    preserve_whitespace_tag_stack: Any
    string_container_stack: Any
    def reset(self) -> None: ...
    def new_tag(
        self,
        name,
        namespace: Incomplete | None = None,
        nsprefix: Incomplete | None = None,
        attrs={},
        sourceline: Incomplete | None = None,
        sourcepos: Incomplete | None = None,
        **kwattrs,
    ) -> Tag: ...
    def string_container(self, base_class: Incomplete | None = None): ...
    def new_string(self, s, subclass: Incomplete | None = None): ...
    def insert_before(self, *args) -> None: ...
    def insert_after(self, *args) -> None: ...
    def popTag(self): ...
    def pushTag(self, tag) -> None: ...
    def endData(self, containerClass: Incomplete | None = None) -> None: ...
    def object_was_parsed(self, o, parent: Incomplete | None = None, most_recent_element: Incomplete | None = None) -> None: ...
    def handle_starttag(
        self,
        name,
        namespace,
        nsprefix,
        attrs,
        sourceline: Incomplete | None = None,
        sourcepos: Incomplete | None = None,
        namespaces: dict[str, str] | None = None,
    ): ...
    def handle_endtag(self, name, nsprefix: Incomplete | None = None) -> None: ...
    def handle_data(self, data) -> None: ...
    def decode(  # type: ignore[override]
        self,
        pretty_print: bool = False,
        eventual_encoding: str = "utf-8",
        formatter: str | Formatter = "minimal",
        iterator: Iterator[PageElement] | None = None,
    ): ...  # missing some arguments

class BeautifulStoneSoup(BeautifulSoup): ...
class StopParsing(Exception): ...
class FeatureNotFound(ValueError): ...
