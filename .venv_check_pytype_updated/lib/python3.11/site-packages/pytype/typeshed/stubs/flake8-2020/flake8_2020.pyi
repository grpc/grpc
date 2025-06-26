# flake8-2020 has type annotations, but PEP 561 states:
# This PEP does not support distributing typing information as part of module-only distributions or single-file modules within namespace packages.
# Therefore typeshed is the best place.

import ast
from collections.abc import Generator
from typing import Any, ClassVar

YTT101: str
YTT102: str
YTT103: str
YTT201: str
YTT202: str
YTT203: str
YTT204: str
YTT301: str
YTT302: str
YTT303: str

class Visitor(ast.NodeVisitor): ...

class Plugin:
    name: ClassVar[str]
    version: ClassVar[str]
    def __init__(self, tree: ast.AST) -> None: ...
    def run(self) -> Generator[tuple[int, int, str, type[Any]], None, None]: ...
