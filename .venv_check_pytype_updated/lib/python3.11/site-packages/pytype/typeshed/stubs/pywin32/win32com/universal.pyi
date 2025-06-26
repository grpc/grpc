from _typeshed import Incomplete

from pythoncom import com_error as com_error
from win32com.client import gencache as gencache

def RegisterInterfaces(typelibGUID, lcid, major, minor, interface_names: Incomplete | None = ...): ...

class Arg:
    name: Incomplete
    size: Incomplete
    offset: int
    def __init__(self, arg_info, name: Incomplete | None = ...) -> None: ...

class Method:
    dispid: Incomplete
    invkind: Incomplete
    name: Incomplete
    args: Incomplete
    cbArgs: Incomplete
    def __init__(self, method_info, isEventSink: int = ...) -> None: ...

class Definition:
    def __init__(self, iid, is_dispatch, method_defs) -> None: ...
    def iid(self): ...
    def vtbl_argsizes(self): ...
    def vtbl_argcounts(self): ...
    def dispatch(self, ob, index, argPtr, ReadFromInTuple=..., WriteFromOutTuple=...): ...
