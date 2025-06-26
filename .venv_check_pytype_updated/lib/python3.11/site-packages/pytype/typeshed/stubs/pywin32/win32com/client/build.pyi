from _typeshed import Incomplete

class OleItem:
    typename: str
    doc: Incomplete
    python_name: Incomplete
    bWritten: int
    bIsDispatch: int
    bIsSink: int
    clsid: Incomplete
    co_class: Incomplete
    def __init__(self, doc: Incomplete | None = ...) -> None: ...

class DispatchItem(OleItem):
    typename: str
    propMap: Incomplete
    propMapGet: Incomplete
    propMapPut: Incomplete
    mapFuncs: Incomplete
    defaultDispatchName: Incomplete
    hidden: int
    def __init__(
        self, typeinfo: Incomplete | None = ..., attr: Incomplete | None = ..., doc: Incomplete | None = ..., bForUser: int = ...
    ) -> None: ...
    clsid: Incomplete
    bIsDispatch: Incomplete
    def Build(self, typeinfo, attr, bForUser: int = ...) -> None: ...
    def CountInOutOptArgs(self, argTuple): ...
    def MakeFuncMethod(self, entry, name, bMakeClass: int = ...): ...
    def MakeDispatchFuncMethod(self, entry, name, bMakeClass: int = ...): ...
    def MakeVarArgsFuncMethod(self, entry, name, bMakeClass: int = ...): ...

class LazyDispatchItem(DispatchItem):
    typename: str
    clsid: Incomplete
    def __init__(self, attr, doc) -> None: ...
