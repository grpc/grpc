from collections.abc import Callable, Iterable

import gdb
from gdb import _PrettyPrinterLookupFunction

class PrettyPrinter:
    name: str
    subprinters: list[SubPrettyPrinter] | None
    enabled: bool

    def __init__(self, name: str, subprinters: Iterable[SubPrettyPrinter] | None = ...) -> None: ...
    def __call__(self, val: gdb.Value) -> gdb._PrettyPrinter | None: ...

class SubPrettyPrinter:
    name: str
    enabled: bool

    def __init__(self, name: str) -> None: ...

class RegexpCollectionPrettyPrinter(PrettyPrinter):
    def __init__(self, name: str) -> None: ...
    def add_printer(self, name: str, regexp: str, gen_printer: _PrettyPrinterLookupFunction) -> None: ...

class FlagEnumerationPrinter(PrettyPrinter):
    def __init__(self, enum_type: str) -> None: ...

def register_pretty_printer(
    obj: gdb.Objfile | gdb.Progspace | None,
    printer: PrettyPrinter | Callable[[gdb.Value], gdb._PrettyPrinter | None],
    replace: bool = ...,
) -> None: ...
