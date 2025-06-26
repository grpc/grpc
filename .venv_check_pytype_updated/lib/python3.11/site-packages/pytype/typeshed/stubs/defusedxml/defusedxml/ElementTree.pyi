from _typeshed import Incomplete
from collections.abc import Iterator, Sequence
from typing import Any
from xml.etree.ElementTree import Element, ElementTree, ParseError as ParseError, XMLParser as _XMLParser, tostring as tostring

class DefusedXMLParser(_XMLParser):
    forbid_dtd: bool
    forbid_entities: bool
    forbid_external: bool
    def __init__(
        self,
        html=...,
        target: Incomplete | None = None,
        encoding: str | None = None,
        forbid_dtd: bool = False,
        forbid_entities: bool = True,
        forbid_external: bool = True,
    ) -> None: ...
    def defused_start_doctype_decl(self, name, sysid, pubid, has_internal_subset) -> None: ...
    def defused_entity_decl(self, name, is_parameter_entity, value, base, sysid, pubid, notation_name) -> None: ...
    def defused_unparsed_entity_decl(self, name, base, sysid, pubid, notation_name) -> None: ...
    def defused_external_entity_ref_handler(self, context, base, sysid, pubid) -> None: ...

XMLTreeBuilder = DefusedXMLParser
XMLParse = DefusedXMLParser
XMLParser = DefusedXMLParser

# wrapper to xml.etree.ElementTree.parse
def parse(
    source, parser: XMLParser | None = None, forbid_dtd: bool = False, forbid_entities: bool = True, forbid_external: bool = True
) -> ElementTree: ...

# wrapper to xml.etree.ElementTree.iterparse
def iterparse(
    source,
    events: Sequence[str] | None = None,
    parser: XMLParser | None = None,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> Iterator[tuple[str, Any]]: ...
def fromstring(text, forbid_dtd: bool = False, forbid_entities: bool = True, forbid_external: bool = True) -> Element: ...

XML = fromstring

__all__ = ["ParseError", "XML", "XMLParse", "XMLParser", "XMLTreeBuilder", "fromstring", "iterparse", "parse", "tostring"]
