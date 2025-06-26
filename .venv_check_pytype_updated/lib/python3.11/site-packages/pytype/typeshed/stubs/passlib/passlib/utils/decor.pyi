from _typeshed import Incomplete
from typing import Any

class classproperty:
    im_func: Any
    def __init__(self, func) -> None: ...
    def __get__(self, obj, cls): ...
    @property
    def __func__(self): ...

class hybrid_method:
    func: Any
    def __init__(self, func) -> None: ...
    def __get__(self, obj, cls): ...

def memoize_single_value(func): ...

class memoized_property:
    __func__: Any
    __name__: Any
    __doc__: Any
    def __init__(self, func) -> None: ...
    def __get__(self, obj, cls): ...
    def clear_cache(self, obj) -> None: ...
    def peek_cache(self, obj, default: Incomplete | None = None): ...

def deprecated_function(
    msg: Incomplete | None = None,
    deprecated: Incomplete | None = None,
    removed: Incomplete | None = None,
    updoc: bool = True,
    replacement: Incomplete | None = None,
    _is_method: bool = False,
    func_module: Incomplete | None = None,
): ...
def deprecated_method(
    msg: Incomplete | None = None,
    deprecated: Incomplete | None = None,
    removed: Incomplete | None = None,
    updoc: bool = True,
    replacement: Incomplete | None = None,
): ...
