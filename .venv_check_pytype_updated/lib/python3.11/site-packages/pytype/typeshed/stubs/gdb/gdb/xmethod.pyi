from collections.abc import Sequence
from typing import Protocol

import gdb

def register_xmethod_matcher(
    locus: gdb.Objfile | gdb.Progspace | None, matcher: _XMethodMatcher, replace: bool = ...
) -> None: ...

class _XMethod(Protocol):
    name: str
    enabled: bool

class XMethod:
    name: str
    enabled: bool

    def __init__(self, name: str) -> None: ...

class _XMethodWorker(Protocol):
    def get_arg_types(self) -> Sequence[gdb.Type]: ...
    def get_result_type(self, *args: gdb.Value) -> gdb.Type: ...
    def __call__(self, *args: gdb.Value) -> object: ...

class XMethodWorker: ...

class _XMethodMatcher(Protocol):
    enabled: bool
    methods: list[_XMethod]

    def __init__(self, name: str) -> None: ...
    def match(self, class_type: gdb.Type, method_name: str) -> _XMethodWorker: ...

class XMethodMatcher:
    def __init__(self, name: str) -> None: ...
