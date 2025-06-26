from _typeshed import Incomplete
from collections.abc import Callable, Sequence
from re import Match, Pattern
from types import ModuleType
from typing import Any
from typing_extensions import TypeAlias

from docutils import nodes
from docutils.utils import Reporter

class Struct:
    def __init__(self, **keywordargs) -> None: ...

_BasicDefinition: TypeAlias = tuple[str, str, str, list[Pattern[str]]]
_DefinitionParts: TypeAlias = tuple[str, str, str, list[Pattern[str] | _BasicDefinition]]
_DefinitionType: TypeAlias = tuple[str, str, str, list[Pattern[str] | _DefinitionParts]]

class Inliner:
    implicit_dispatch: list[tuple[Pattern[str], Callable[[Match[str], int], Sequence[nodes.Node]]]]
    def __init__(self) -> None: ...
    start_string_prefix: str
    end_string_suffix: str
    parts: _DefinitionType
    patterns: Any
    def init_customizations(self, settings: Any) -> None: ...
    reporter: Reporter
    document: nodes.document
    language: ModuleType
    parent: nodes.Element
    def parse(
        self, text: str, lineno: int, memo: Struct, parent: nodes.Element
    ) -> tuple[list[nodes.Node], list[nodes.system_message]]: ...
    non_whitespace_before: str
    non_whitespace_escape_before: str
    non_unescaped_whitespace_escape_before: str
    non_whitespace_after: str
    simplename: str
    uric: str
    uri_end_delim: str
    urilast: str
    uri_end: str
    emailc: str
    email_pattern: str
    def quoted_start(self, match: Match[str]) -> bool: ...
    def inline_obj(
        self,
        match: Match[str],
        lineno: int,
        end_pattern: Pattern[str],
        nodeclass: nodes.TextElement,
        restore_backslashes: bool = False,
    ) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message], str]: ...
    def problematic(self, text: str, rawsource: str, message: nodes.system_message) -> nodes.problematic: ...
    def emphasis(
        self, match: Match[str], lineno: int
    ) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def strong(self, match: Match[str], lineno: int) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def interpreted_or_phrase_ref(
        self, match: Match[str], lineno: int
    ) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def phrase_ref(
        self, before: str, after: str, rawsource: str, escaped: str, text: str | None = None
    ) -> tuple[str, list[nodes.Node], str, list[nodes.Node]]: ...
    def adjust_uri(self, uri: str) -> str: ...
    def interpreted(
        self, rawsource: str, text: str, role: str, lineno: int
    ) -> tuple[list[nodes.Node], list[nodes.system_message]]: ...
    def literal(self, match: Match[str], lineno: int) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def inline_internal_target(
        self, match: Match[str], lineno: int
    ) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def substitution_reference(
        self, match: Match[str], lineno: int
    ) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def footnote_reference(
        self, match: Match[str], lineno: int
    ) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def reference(
        self, match: Match[str], lineno: int, anonymous: bool = ...
    ) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def anonymous_reference(
        self, match: Match[str], lineno: int
    ) -> tuple[str, list[nodes.problematic], str, list[nodes.system_message]]: ...
    def standalone_uri(
        self, match: Match[str], lineno: int
    ) -> list[tuple[str, list[nodes.problematic], str, list[nodes.system_message]]]: ...
    def pep_reference(
        self, match: Match[str], lineno: int
    ) -> list[tuple[str, list[nodes.problematic], str, list[nodes.system_message]]]: ...
    rfc_url: str = ...
    def rfc_reference(
        self, match: Match[str], lineno: int
    ) -> list[tuple[str, list[nodes.problematic], str, list[nodes.system_message]]]: ...
    def implicit_inline(self, text: str, lineno: int) -> list[nodes.Text]: ...
    dispatch: dict[str, Callable[[Match[str], int], tuple[str, list[nodes.problematic], str, list[nodes.system_message]]]] = ...

def __getattr__(name: str) -> Incomplete: ...
