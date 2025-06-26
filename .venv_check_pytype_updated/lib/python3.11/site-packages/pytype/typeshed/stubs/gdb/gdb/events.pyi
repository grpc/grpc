from collections.abc import Callable, Sequence

import gdb

class ThreadEvent:
    inferior_thread: gdb.InferiorThread

class ContinueEvent(ThreadEvent): ...

class ContinueEventRegistry:
    def connect(self, object: Callable[[ContinueEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[ContinueEvent], object], /) -> None: ...

cont: ContinueEventRegistry

class ExitedEvent:
    exit_code: int
    inferior: gdb.Inferior

class ExitedEventRegistry:
    def connect(self, object: Callable[[ExitedEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[ExitedEvent], object], /) -> None: ...

exited: ExitedEventRegistry

class StopEvent(ThreadEvent):
    stop_signal: str

class BreakpointEvent(StopEvent):
    breakpoints: Sequence[gdb.Breakpoint]
    breakpoint: gdb.Breakpoint

class StopEventRegistry:
    def connect(self, object: Callable[[StopEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[StopEvent], object], /) -> None: ...

stop: StopEventRegistry

class NewObjFileEvent:
    new_objfile: gdb.Objfile

class NewObjFileEventRegistry:
    def connect(self, object: Callable[[NewObjFileEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[NewObjFileEvent], object], /) -> None: ...

new_objfile: NewObjFileEventRegistry

class ClearObjFilesEvent:
    progspace: gdb.Progspace

class ClearObjFilesEventRegistry:
    def connect(self, object: Callable[[ClearObjFilesEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[ClearObjFilesEvent], object], /) -> None: ...

clear_objfiles: ClearObjFilesEventRegistry

class InferiorCallEvent: ...

class InferiorCallPreEvent(InferiorCallEvent):
    ptid: gdb.InferiorThread
    address: gdb.Value

class InferiorCallPostEvent(InferiorCallEvent):
    ptid: gdb.InferiorThread
    address: gdb.Value

class InferiorCallEventRegistry:
    def connect(self, object: Callable[[InferiorCallEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[InferiorCallEvent], object], /) -> None: ...

inferior_call: InferiorCallEventRegistry

class MemoryChangedEvent:
    address: gdb.Value
    length: int

class MemoryChangedEventRegistry:
    def connect(self, object: Callable[[MemoryChangedEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[MemoryChangedEvent], object], /) -> None: ...

memory_changed: MemoryChangedEventRegistry

class RegisterChangedEvent:
    frame: gdb.Frame
    regnum: str

class RegisterChangedEventRegistry:
    def connect(self, object: Callable[[RegisterChangedEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[RegisterChangedEvent], object], /) -> None: ...

register_changed: RegisterChangedEventRegistry

class BreakpointEventRegistry:
    def connect(self, object: Callable[[gdb.Breakpoint], object], /) -> None: ...
    def disconnect(self, object: Callable[[gdb.Breakpoint], object], /) -> None: ...

breakpoint_created: BreakpointEventRegistry
breakpoint_modified: BreakpointEventRegistry
breakpoint_deleted: BreakpointEventRegistry

class BeforePromptEventRegistry:
    def connect(self, object: Callable[[], object], /) -> None: ...
    def disconnect(self, object: Callable[[], object], /) -> None: ...

before_prompt: BeforePromptEventRegistry

class NewInferiorEvent:
    inferior: gdb.Inferior

class NewInferiorEventRegistry:
    def connect(self, object: Callable[[NewInferiorEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[NewInferiorEvent], object], /) -> None: ...

new_inferior: NewInferiorEventRegistry

class InferiorDeletedEvent:
    inferior: gdb.Inferior

class InferiorDeletedEventRegistry:
    def connect(self, object: Callable[[InferiorDeletedEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[InferiorDeletedEvent], object], /) -> None: ...

inferior_deleted: InferiorDeletedEventRegistry

class NewThreadEvent(ThreadEvent): ...

class NewThreadEventRegistry:
    def connect(self, object: Callable[[NewThreadEvent], object], /) -> None: ...
    def disconnect(self, object: Callable[[NewThreadEvent], object], /) -> None: ...

new_thread: NewThreadEventRegistry
