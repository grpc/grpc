from _typeshed import Incomplete
from collections.abc import Callable, Sequence
from typing import Any, ClassVar, Literal
from typing_extensions import TypeAlias

from docutils import nodes, parsers
from docutils.parsers.rst.states import Inliner, RSTState, RSTStateMachine
from docutils.statemachine import StringList
from docutils.transforms import Transform

class Parser(parsers.Parser):
    settings_spec: ClassVar[Incomplete]
    config_section_dependencies: ClassVar[tuple[str, ...]]
    initial_state: Literal["Body", "RFC2822Body"]
    state_classes: Sequence[type[RSTState]]
    inliner: Inliner | None
    def __init__(self, rfc2822: bool = False, inliner: Inliner | None = None) -> None: ...
    def get_transforms(self) -> list[type[Transform]]: ...
    def parse(self, inputstring: str, document: nodes.document) -> None: ...

class DirectiveError(Exception):
    level: int
    msg: str
    def __init__(self, level: int, message: str) -> None: ...

class Directive:
    required_arguments: ClassVar[int]
    optional_arguments: ClassVar[int]
    final_argument_whitespace: ClassVar[bool]
    option_spec: ClassVar[dict[str, Callable[[str], Any]] | None]
    has_content: ClassVar[bool]
    name: str
    arguments: list[str]
    options: dict[str, Any]
    content: StringList
    lineno: int
    content_offset: int
    block_text: str
    state: RSTState
    state_machine: RSTStateMachine = ...
    def __init__(
        self,
        name: str,
        arguments: list[str],
        options: dict[str, Any],
        content: StringList,
        lineno: int,
        content_offset: int,
        block_text: str,
        state: RSTState,
        state_machine: RSTStateMachine,
    ) -> None: ...
    def run(self) -> Sequence[nodes.Node]: ...
    def directive_error(self, level: int, message: str) -> DirectiveError: ...
    def debug(self, message: str) -> DirectiveError: ...
    def info(self, message: str) -> DirectiveError: ...
    def warning(self, message: str) -> DirectiveError: ...
    def error(self, message: str) -> DirectiveError: ...
    def severe(self, message: str) -> DirectiveError: ...
    def assert_has_content(self) -> None: ...
    def add_name(self, node: nodes.Node) -> None: ...

_DirectiveFn: TypeAlias = Callable[
    [str, list[str], dict[str, Any], StringList, int, int, str, RSTState, RSTStateMachine], Directive
]

def convert_directive_function(directive_fn: _DirectiveFn) -> type[Directive]: ...
