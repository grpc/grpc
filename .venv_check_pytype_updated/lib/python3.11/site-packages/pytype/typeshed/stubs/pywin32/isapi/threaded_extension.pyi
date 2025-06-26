import threading
from _typeshed import Incomplete

import isapi.simple
from isapi import ExtensionError as ExtensionError, isapicon as isapicon
from win32event import INFINITE as INFINITE

ISAPI_REQUEST: int
ISAPI_SHUTDOWN: int

class WorkerThread(threading.Thread):
    running: bool
    io_req_port: Incomplete
    extension: Incomplete
    def __init__(self, extension, io_req_port) -> None: ...
    def call_handler(self, cblock) -> None: ...

class ThreadPoolExtension(isapi.simple.SimpleExtension):
    max_workers: int
    worker_shutdown_wait: int
    workers: Incomplete
    dispatch_map: Incomplete
    io_req_port: Incomplete
    def GetExtensionVersion(self, vi) -> None: ...
    def HttpExtensionProc(self, control_block): ...
    def TerminateExtension(self, status) -> None: ...
    def DispatchConnection(self, errCode, bytes, key, overlapped) -> None: ...
    def Dispatch(self, ecb) -> None: ...
    def HandleDispatchError(self, ecb) -> None: ...
