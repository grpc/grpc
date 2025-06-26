from _typeshed import Incomplete
from collections.abc import Iterable
from typing import Any, ClassVar
from typing_extensions import TypeAlias

__version__: str

_Argv: TypeAlias = Iterable[str] | str

class DocoptLanguageError(Exception): ...

class DocoptExit(SystemExit):
    usage: ClassVar[str]
    def __init__(self, message: str = "") -> None: ...

def printable_usage(doc: str) -> str: ...
def docopt(
    doc: str, argv: _Argv | None = ..., help: bool = ..., version: Incomplete | None = ..., options_first: bool = ...
) -> dict[str, Any]: ...  # Really should be dict[str, str | bool]
