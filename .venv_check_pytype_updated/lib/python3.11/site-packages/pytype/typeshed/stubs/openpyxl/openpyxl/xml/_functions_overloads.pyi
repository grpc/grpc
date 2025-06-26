# This file does not exist at runtime. It is a helper file to overload imported functions in openpyxl.xml.functions

from _typeshed import Incomplete, ReadableBuffer
from collections.abc import Iterable, Iterator, Mapping, Sequence
from typing import Any, Protocol, TypeVar, overload
from typing_extensions import TypeAlias
from xml.etree.ElementTree import Element, ElementTree, QName, XMLParser, _FileRead

from openpyxl.chart.axis import ChartLines

_T = TypeVar("_T")
_T_co = TypeVar("_T_co", covariant=True)

# Useful protocols, see import comment in openpyxl/xml/functions.pyi

# Comment from openpyxl.cell.rich_text.py
# Usually an Element() from either lxml or xml.etree (has a 'tag' element)
# lxml.etree._Element
# xml.etree.Element
class _HasTag(Protocol):
    tag: str

class _HasGet(Protocol[_T_co]):
    def get(self, value: str, /) -> _T_co | None: ...

class _HasText(Protocol):
    text: str

class _HasAttrib(Protocol):
    attrib: Iterable[Any]  # AnyOf[dict[str, str], Iterable[tuple[str, str]]]

class _HasTagAndGet(_HasTag, _HasGet[_T_co], Protocol[_T_co]): ...  # noqa: Y046
class _HasTagAndText(_HasTag, _HasText, Protocol): ...  # noqa: Y046
class _HasTagAndTextAndAttrib(_HasTag, _HasText, _HasAttrib, Protocol): ...  # noqa: Y046

class _SupportsFindChartLines(Protocol):
    def find(self, path: str, /) -> ChartLines | None: ...

class _SupportsFindAndIterAndAttribAndText(  # noqa: Y046
    _SupportsFindChartLines, Iterable[Incomplete], _HasAttrib, _HasText, Protocol
): ...
class _SupportsIterAndAttrib(Iterable[Incomplete], _HasAttrib, Protocol): ...  # noqa: Y046
class _SupportsIterAndAttribAndTextAndTag(Iterable[Incomplete], _HasAttrib, _HasText, _HasTag, Protocol): ...  # noqa: Y046
class _SupportsIterAndAttribAndTextAndGet(  # noqa: Y046
    Iterable[Incomplete], _HasAttrib, _HasText, _HasGet[Incomplete], Protocol
): ...

class _ParentElement(Protocol[_T]):
    def makeelement(self, tag: str, attrib: dict[str, str], /) -> _T: ...
    def append(self, element: _T, /) -> object: ...

# from lxml.etree import _Element
_lxml_Element: TypeAlias = Element  # noqa: Y042
# from lxml.etree import _ElementTree
_lxml_ElementTree: TypeAlias = ElementTree  # noqa: Y042
# from lxml.etree import QName
_lxml_QName: TypeAlias = QName  # noqa: Y042

# from xml.etree import fromstring
@overload
def SubElement(parent: _ParentElement[_T], tag: str, attrib: dict[str, str] = ..., **extra: str) -> _T: ...

# from lxml.etree import fromstring
@overload
def SubElement(
    _parent: _lxml_Element,  # This would be preferable as a protocol, but it's a C-Extension
    _tag: str | bytes | _lxml_QName,
    attrib: dict[str, str] | dict[bytes, bytes] | None = ...,
    nsmap: Mapping[str, str] | None = ...,
    **extra: str | bytes,
) -> _lxml_ElementTree: ...

# from xml.etree.ElementTree import fromstring
@overload
def fromstring(text: str | ReadableBuffer, parser: XMLParser | None = None) -> Element: ...

# from lxml.etree import fromstring
# But made partial, removing parser arg
@overload
def fromstring(text: str | bytes, *, base_url: str | bytes = ...) -> _lxml_Element: ...  # type: ignore[overload-overlap]

# from defusedxml.ElementTree import fromstring
@overload
def fromstring(text: str, forbid_dtd: bool = False, forbid_entities: bool = True, forbid_external: bool = True) -> int: ...

# from xml.etree.ElementTree import tostring
# But made partial, removing encoding arg
@overload
def tostring(
    element: Element,
    method: str | None = "xml",
    *,
    xml_declaration: bool | None = None,
    default_namespace: str | None = ...,
    short_empty_elements: bool = ...,
) -> str: ...

# from lxml.etree import Element
# But made partial, removing encoding arg
@overload
def tostring(
    element_or_tree: _lxml_Element | _lxml_ElementTree,
    method: str = ...,
    xml_declaration: bool = ...,
    pretty_print: bool = ...,
    with_tail: bool = ...,
    standalone: bool = ...,
    doctype: str = ...,
    exclusive: bool = ...,
    with_comments: bool = ...,
    inclusive_ns_prefixes: Incomplete = ...,
) -> bytes: ...

# from xml.etree.ElementTree import iterparse
@overload
def iterparse(
    source: _FileRead, events: Sequence[str] | None = None, parser: XMLParser | None = None
) -> Iterator[tuple[str, Any]]: ...

# from defusedxml.ElementTree import iterparse
@overload
def iterparse(
    source: _FileRead,
    events: Sequence[str] | None = None,
    parser: XMLParser | None = None,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> Iterator[tuple[str, Any]]: ...
