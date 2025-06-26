from _typeshed import Incomplete
from typing_extensions import TypeAlias, deprecated

from win32com.server.exception import IsCOMServerException as IsCOMServerException
from win32com.util import IIDToInterfaceName as IIDToInterfaceName

class DispatcherBase:
    policy: Incomplete
    logger: Incomplete
    def __init__(self, policyClass, object) -> None: ...

class DispatcherTrace(DispatcherBase): ...

class DispatcherWin32trace(DispatcherTrace):
    def __init__(self, policyClass, object) -> None: ...

class DispatcherOutputDebugString(DispatcherTrace): ...

@deprecated(
    """\
The DispatcherWin32dbg dispatcher is deprecated!
Please let the developer know if this is a problem.
Uncomment the relevant lines in dispatcher.py to re-enable"""
)
class DispatcherWin32dbg(DispatcherBase):
    def __init__(self, policyClass, ob) -> None: ...

DefaultDebugDispatcher: TypeAlias = DispatcherTrace
