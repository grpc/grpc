from _typeshed import Incomplete

import win32com.server.policy

debugging: int

def trace(*args) -> None: ...

all_wrapped: Incomplete

def RaiseNotImpl(who: Incomplete | None = ...) -> None: ...

class Dispatcher(win32com.server.policy.DispatcherWin32trace):
    def __init__(self, policyClass, object) -> None: ...
