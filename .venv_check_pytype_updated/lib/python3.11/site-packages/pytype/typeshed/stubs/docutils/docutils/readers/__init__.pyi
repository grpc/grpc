from typing import Any, ClassVar, Generic, TypeVar

from docutils import Component, nodes
from docutils.frontend import Values
from docutils.io import Input
from docutils.parsers import Parser
from docutils.transforms import Transform

_S = TypeVar("_S")

class Reader(Component, Generic[_S]):
    component_type: ClassVar[str]
    config_section: ClassVar[str]
    def get_transforms(self) -> list[type[Transform]]: ...
    def __init__(self, parser: Parser | None = None, parser_name: str | None = None) -> None: ...
    parser: Parser | None
    source: Input[_S] | None
    input: str | Any | None
    def set_parser(self, parser_name: str) -> None: ...
    def read(self, source: Input[_S], parser: Parser, settings: Values) -> nodes.document: ...
    document: nodes.document
    def parse(self) -> None: ...
    def new_document(self) -> nodes.document: ...

class ReReader(Reader[_S]):
    def get_transforms(self) -> list[type[Transform]]: ...

def get_reader_class(reader_name: str) -> type[Reader[Any]]: ...
