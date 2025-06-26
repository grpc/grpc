from collections.abc import Callable
from typing_extensions import TypeAlias

from .dammit import EntitySubstitution as EntitySubstitution

_EntitySubstitution: TypeAlias = Callable[[str], str]

class Formatter(EntitySubstitution):
    HTML: str
    XML: str
    HTML_DEFAULTS: dict[str, set[str]]
    language: str | None
    entity_substitution: _EntitySubstitution
    void_element_close_prefix: str
    cdata_containing_tags: list[str]
    empty_attributes_are_booleans: bool
    def __init__(
        self,
        language: str | None = None,
        entity_substitution: _EntitySubstitution | None = None,
        void_element_close_prefix: str = "/",
        cdata_containing_tags: list[str] | None = None,
        empty_attributes_are_booleans: bool = False,
        indent: int = 1,
    ) -> None: ...
    def substitute(self, ns: str) -> str: ...
    def attribute_value(self, value: str) -> str: ...
    def attributes(self, tag): ...

class HTMLFormatter(Formatter):
    REGISTRY: dict[str, HTMLFormatter]
    def __init__(
        self,
        entity_substitution: _EntitySubstitution | None = ...,
        void_element_close_prefix: str = ...,
        cdata_containing_tags: list[str] | None = ...,
    ) -> None: ...

class XMLFormatter(Formatter):
    REGISTRY: dict[str, XMLFormatter]
    def __init__(
        self,
        entity_substitution: _EntitySubstitution | None = ...,
        void_element_close_prefix: str = ...,
        cdata_containing_tags: list[str] | None = ...,
    ) -> None: ...
