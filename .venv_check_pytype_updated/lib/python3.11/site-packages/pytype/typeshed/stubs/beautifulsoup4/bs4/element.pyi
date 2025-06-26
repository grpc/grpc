from _typeshed import Incomplete, ReadableBuffer
from collections.abc import Callable, Iterable, Iterator
from re import Pattern
from typing import Any, TypeVar, overload
from typing_extensions import Self, TypeAlias

from . import BeautifulSoup
from .builder import TreeBuilder
from .formatter import Formatter, _EntitySubstitution

DEFAULT_OUTPUT_ENCODING: str
nonwhitespace_re: Pattern[str]
whitespace_re: Pattern[str]
PYTHON_SPECIFIC_ENCODINGS: set[str]

class NamespacedAttribute(str):
    def __new__(cls, prefix: str, name: str | None = None, namespace: str | None = None) -> Self: ...

class AttributeValueWithCharsetSubstitution(str): ...

class CharsetMetaAttributeValue(AttributeValueWithCharsetSubstitution):
    def __new__(cls, original_value): ...
    def encode(self, encoding: str) -> str: ...  # type: ignore[override]  # incompatible with str

class ContentMetaAttributeValue(AttributeValueWithCharsetSubstitution):
    CHARSET_RE: Pattern[str]
    def __new__(cls, original_value): ...
    def encode(self, encoding: str) -> str: ...  # type: ignore[override]  # incompatible with str

_PageElementT = TypeVar("_PageElementT", bound=PageElement)
_SimpleStrainable: TypeAlias = str | bool | None | bytes | Pattern[str] | Callable[[str], bool] | Callable[[Tag], bool]
_Strainable: TypeAlias = _SimpleStrainable | Iterable[_SimpleStrainable]
_SimpleNormalizedStrainable: TypeAlias = str | bool | None | Pattern[str] | Callable[[str], bool] | Callable[[Tag], bool]
_NormalizedStrainable: TypeAlias = _SimpleNormalizedStrainable | Iterable[_SimpleNormalizedStrainable]

class PageElement:
    parent: Tag | None
    previous_element: PageElement | None
    next_element: PageElement | None
    next_sibling: PageElement | None
    previous_sibling: PageElement | None
    def setup(
        self,
        parent: Tag | None = None,
        previous_element: PageElement | None = None,
        next_element: PageElement | None = None,
        previous_sibling: PageElement | None = None,
        next_sibling: PageElement | None = None,
    ) -> None: ...
    def format_string(self, s: str, formatter: Formatter | str | None) -> str: ...
    def formatter_for_name(self, formatter: Formatter | str | _EntitySubstitution): ...
    nextSibling: PageElement | None
    previousSibling: PageElement | None
    @property
    def stripped_strings(self) -> Iterator[str]: ...
    def get_text(self, separator: str = "", strip: bool = False, types: tuple[type[NavigableString], ...] = ...) -> str: ...
    getText = get_text
    @property
    def text(self) -> str: ...
    def replace_with(self, *args: PageElement | str) -> Self: ...
    replaceWith = replace_with
    def unwrap(self) -> Self: ...
    replace_with_children = unwrap
    replaceWithChildren = unwrap
    def wrap(self, wrap_inside: _PageElementT) -> _PageElementT: ...
    def extract(self, _self_index: int | None = None) -> Self: ...
    def insert(self, position: int, new_child: PageElement | str) -> None: ...
    def append(self, tag: PageElement | str) -> None: ...
    def extend(self, tags: Iterable[PageElement | str]) -> None: ...
    def insert_before(self, *args: PageElement | str) -> None: ...
    def insert_after(self, *args: PageElement | str) -> None: ...
    def find_next(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        **kwargs: _Strainable,
    ) -> Tag | NavigableString | None: ...
    findNext = find_next
    def find_all_next(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        limit: int | None = None,
        **kwargs: _Strainable,
    ) -> ResultSet[PageElement]: ...
    findAllNext = find_all_next
    def find_next_sibling(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        **kwargs: _Strainable,
    ) -> Tag | NavigableString | None: ...
    findNextSibling = find_next_sibling
    def find_next_siblings(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        limit: int | None = None,
        **kwargs: _Strainable,
    ) -> ResultSet[PageElement]: ...
    findNextSiblings = find_next_siblings
    fetchNextSiblings = find_next_siblings
    def find_previous(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        **kwargs: _Strainable,
    ) -> Tag | NavigableString | None: ...
    findPrevious = find_previous
    def find_all_previous(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        limit: int | None = None,
        **kwargs: _Strainable,
    ) -> ResultSet[PageElement]: ...
    findAllPrevious = find_all_previous
    fetchPrevious = find_all_previous
    def find_previous_sibling(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        **kwargs: _Strainable,
    ) -> Tag | NavigableString | None: ...
    findPreviousSibling = find_previous_sibling
    def find_previous_siblings(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        limit: int | None = None,
        **kwargs: _Strainable,
    ) -> ResultSet[PageElement]: ...
    findPreviousSiblings = find_previous_siblings
    fetchPreviousSiblings = find_previous_siblings
    def find_parent(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        **kwargs: _Strainable,
    ) -> Tag | None: ...
    findParent = find_parent
    def find_parents(
        self,
        name: _Strainable | SoupStrainer | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        limit: int | None = None,
        **kwargs: _Strainable,
    ) -> ResultSet[Tag]: ...
    findParents = find_parents
    fetchParents = find_parents
    @property
    def next(self) -> Tag | NavigableString | None: ...
    @property
    def previous(self) -> Tag | NavigableString | None: ...
    @property
    def next_elements(self) -> Iterable[PageElement]: ...
    @property
    def next_siblings(self) -> Iterable[PageElement]: ...
    @property
    def previous_elements(self) -> Iterable[PageElement]: ...
    @property
    def previous_siblings(self) -> Iterable[PageElement]: ...
    @property
    def parents(self) -> Iterable[Tag]: ...
    @property
    def decomposed(self) -> bool: ...
    def nextGenerator(self) -> Iterable[PageElement]: ...
    def nextSiblingGenerator(self) -> Iterable[PageElement]: ...
    def previousGenerator(self) -> Iterable[PageElement]: ...
    def previousSiblingGenerator(self) -> Iterable[PageElement]: ...
    def parentGenerator(self) -> Iterable[Tag]: ...

class NavigableString(str, PageElement):
    PREFIX: str
    SUFFIX: str
    known_xml: bool | None
    def __new__(cls, value: str | ReadableBuffer) -> Self: ...
    def __copy__(self) -> Self: ...
    def __getnewargs__(self) -> tuple[str]: ...
    def output_ready(self, formatter: Formatter | str | None = "minimal") -> str: ...
    @property
    def name(self) -> None: ...
    @property
    def strings(self) -> Iterable[str]: ...

class PreformattedString(NavigableString):
    PREFIX: str
    SUFFIX: str
    def output_ready(self, formatter: Formatter | str | None = None) -> str: ...

class CData(PreformattedString):
    PREFIX: str
    SUFFIX: str

class ProcessingInstruction(PreformattedString):
    PREFIX: str
    SUFFIX: str

class XMLProcessingInstruction(ProcessingInstruction):
    PREFIX: str
    SUFFIX: str

class Comment(PreformattedString):
    PREFIX: str
    SUFFIX: str

class Declaration(PreformattedString):
    PREFIX: str
    SUFFIX: str

class Doctype(PreformattedString):
    @classmethod
    def for_name_and_ids(cls, name: str | None, pub_id: str, system_id: str) -> Doctype: ...
    PREFIX: str
    SUFFIX: str

class Stylesheet(NavigableString): ...
class Script(NavigableString): ...
class TemplateString(NavigableString): ...

class Tag(PageElement):
    parser_class: type[BeautifulSoup] | None
    name: str
    namespace: str | None
    prefix: str | None
    sourceline: int | None
    sourcepos: int | None
    known_xml: bool | None
    attrs: dict[str, str | Any]
    contents: list[PageElement]
    hidden: bool
    can_be_empty_element: bool | None
    cdata_list_attributes: list[str] | None
    preserve_whitespace_tags: list[str] | None
    def __init__(
        self,
        parser: BeautifulSoup | None = None,
        builder: TreeBuilder | None = None,
        name: str | None = None,
        namespace: str | None = None,
        prefix: str | None = None,
        attrs: dict[str, str] | None = None,
        parent: Tag | None = None,
        previous: PageElement | None = None,
        is_xml: bool | None = None,
        sourceline: int | None = None,
        sourcepos: int | None = None,
        can_be_empty_element: bool | None = None,
        cdata_list_attributes: list[str] | None = None,
        preserve_whitespace_tags: list[str] | None = None,
        interesting_string_types: type[NavigableString] | tuple[type[NavigableString], ...] | None = None,
        namespaces: dict[str, str] | None = None,
    ) -> None: ...
    parserClass: type[BeautifulSoup] | None
    def __copy__(self) -> Self: ...
    @property
    def is_empty_element(self) -> bool: ...
    @property
    def isSelfClosing(self) -> bool: ...
    @property
    def string(self) -> str | None: ...
    @string.setter
    def string(self, string: str) -> None: ...
    DEFAULT_INTERESTING_STRING_TYPES: tuple[type[NavigableString], ...]
    @property
    def strings(self) -> Iterable[str]: ...
    def decompose(self) -> None: ...
    def clear(self, decompose: bool = False) -> None: ...
    def smooth(self) -> None: ...
    def index(self, element: PageElement) -> int: ...
    def get(self, key: str, default: str | list[str] | None = None) -> str | list[str] | None: ...
    def get_attribute_list(self, key: str, default: str | list[str] | None = None) -> list[str]: ...
    def has_attr(self, key: str) -> bool: ...
    def __hash__(self) -> int: ...
    def __getitem__(self, key: str) -> str | list[str]: ...
    def __iter__(self) -> Iterator[PageElement]: ...
    def __len__(self) -> int: ...
    def __contains__(self, x: object) -> bool: ...
    def __bool__(self) -> bool: ...
    def __setitem__(self, key: str, value: str | list[str]) -> None: ...
    def __delitem__(self, key: str) -> None: ...
    def __getattr__(self, tag: str) -> Tag | None: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
    def __unicode__(self) -> str: ...
    def encode(
        self,
        encoding: str = "utf-8",
        indent_level: int | None = None,
        formatter: str | Formatter = "minimal",
        errors: str = "xmlcharrefreplace",
    ) -> bytes: ...
    def decode(
        self,
        indent_level: int | None = None,
        eventual_encoding: str = "utf-8",
        formatter: str | Formatter = "minimal",
        iterator: Iterator[PageElement] | None = None,
    ) -> str: ...
    @overload
    def prettify(self, encoding: str, formatter: str | Formatter = "minimal") -> bytes: ...
    @overload
    def prettify(self, encoding: None = None, formatter: str | Formatter = "minimal") -> str: ...
    def decode_contents(
        self, indent_level: int | None = None, eventual_encoding: str = "utf-8", formatter: str | Formatter = "minimal"
    ) -> str: ...
    def encode_contents(
        self, indent_level: int | None = None, encoding: str = "utf-8", formatter: str | Formatter = "minimal"
    ) -> bytes: ...
    def renderContents(self, encoding: str = "utf-8", prettyPrint: bool = False, indentLevel: int = 0) -> bytes: ...
    def find(
        self,
        name: _Strainable | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        recursive: bool = True,
        string: _Strainable | None = None,
        **kwargs: _Strainable,
    ) -> Tag | NavigableString | None: ...
    findChild = find
    def find_all(
        self,
        name: _Strainable | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        recursive: bool = True,
        string: _Strainable | None = None,
        limit: int | None = None,
        **kwargs: _Strainable,
    ) -> ResultSet[Any]: ...
    __call__ = find_all
    findAll = find_all
    findChildren = find_all
    @property
    def children(self) -> Iterable[PageElement]: ...
    @property
    def descendants(self) -> Iterable[PageElement]: ...
    def select_one(
        self, selector: str, namespaces: Incomplete | None = None, *, flags: int = ..., custom: dict[str, str] | None = ...
    ) -> Tag | None: ...
    def select(
        self,
        selector: str,
        namespaces: Incomplete | None = None,
        limit: int | None = None,
        *,
        flags: int = ...,
        custom: dict[str, str] | None = ...,
    ) -> ResultSet[Tag]: ...
    def childGenerator(self) -> Iterable[PageElement]: ...
    def recursiveChildGenerator(self) -> Iterable[PageElement]: ...
    def has_key(self, key: str) -> bool: ...

class SoupStrainer:
    name: _NormalizedStrainable
    attrs: dict[str, _NormalizedStrainable]
    string: _NormalizedStrainable
    def __init__(
        self,
        name: _Strainable | None = None,
        attrs: dict[str, _Strainable] | _Strainable = {},
        string: _Strainable | None = None,
        **kwargs: _Strainable,
    ) -> None: ...
    def search_tag(self, markup_name: Tag | str | None = None, markup_attrs={}): ...
    searchTag = search_tag
    def search(self, markup: PageElement | Iterable[PageElement]): ...

class ResultSet(list[_PageElementT]):
    source: SoupStrainer
    @overload
    def __init__(self, source: SoupStrainer) -> None: ...
    @overload
    def __init__(self, source: SoupStrainer, result: Iterable[_PageElementT]) -> None: ...
