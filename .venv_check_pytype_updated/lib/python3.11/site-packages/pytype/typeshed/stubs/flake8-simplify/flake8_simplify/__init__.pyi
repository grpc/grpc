import ast
from collections.abc import Generator
from typing import Any, ClassVar

class Plugin:
    name: ClassVar[str]
    version: ClassVar[str]
    def __init__(self, tree: ast.AST) -> None: ...
    def run(self) -> Generator[tuple[int, int, str, type[Any]], None, None]: ...
