import ast
from typing import Any

class StrAndRepr: ...

def evaluate_string(string: str | ast.AST) -> Any: ...

class Token(StrAndRepr):
    type: str
    def __init__(self, type: str) -> None: ...
