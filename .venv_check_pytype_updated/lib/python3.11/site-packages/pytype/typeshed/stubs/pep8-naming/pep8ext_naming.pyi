import ast
from _typeshed import Incomplete
from argparse import Namespace
from collections.abc import Generator, Iterable
from typing import Any

__version__: str

PYTHON_VERSION: tuple[int, int, int]
CLASS_METHODS: frozenset[str]
METACLASS_BASES: frozenset[str]
METHOD_CONTAINER_NODES: set[ast.AST]

class NamingChecker:
    name: str
    version: str
    visitors: Any
    decorator_to_type: Any
    ignore_names: frozenset[str]
    parents: Any
    def __init__(self, tree: ast.AST, filename: str) -> None: ...
    @classmethod
    def add_options(cls, parser: Any) -> None: ...
    @classmethod
    def parse_options(cls, option: Namespace) -> None: ...
    def run(self) -> Generator[tuple[int, int, str, type[Any]], None, None]: ...
    def tag_class_functions(self, cls_node: ast.ClassDef) -> None: ...
    def set_function_nodes_types(self, nodes: Iterable[ast.AST], ismetaclass: bool, late_decoration: dict[str, str]) -> None: ...
    def __getattr__(self, name: str) -> Incomplete: ...  # incomplete (other attributes are normally not accessed)

def __getattr__(name: str) -> Incomplete: ...  # incomplete (other attributes are normally not accessed)
