# The GDB Python API is implemented in C, so the type hints below were made
# reading the documentation
# (https://sourceware.org/gdb/onlinedocs/gdb/Python-API.html).

import _typeshed
from collections.abc import Callable, Iterator, Sequence
from contextlib import AbstractContextManager
from typing import Protocol, final, overload
from typing_extensions import TypeAlias

import gdb.types

# The following submodules are automatically imported
from . import events as events, printing as printing, prompt as prompt, types as types

# Basic

VERSION: str

PYTHONDIR: str

STDOUT: int
STDERR: int
STDLOG: int

def execute(command: str, from_tty: bool = ..., to_string: bool = ...) -> str | None: ...
def breakpoints() -> Sequence[Breakpoint]: ...
def rbreak(regex: str, minsyms: bool = ..., throttle: int = ..., symtabs: Iterator[Symtab] = ...) -> list[Breakpoint]: ...
def parameter(parameter: str, /) -> bool | int | str | None: ...
def set_parameter(name: str, value: bool | int | str | None) -> None: ...
def with_parameter(name: str, value: bool | int | str | None) -> AbstractContextManager[None]: ...
def history(number: int, /) -> Value: ...
def add_history(value: Value, /) -> int: ...
def history_count() -> int: ...
def convenience_variable(name: str, /) -> Value | None: ...
def set_convenience_variable(name: str, value: _ValueOrNative | None, /) -> None: ...
def parse_and_eval(expression: str, /) -> Value: ...
def find_pc_line(pc: int | Value) -> Symtab_and_line: ...
def post_event(event: Callable[[], object], /) -> None: ...
def write(string: str, stream: int = ...) -> None: ...
def flush(stream: int = ...) -> None: ...
def target_charset() -> str: ...
def target_wide_charset() -> str: ...
def host_charset() -> str: ...
def solib_name(addr: int) -> str | None: ...
def decode_line(expression: str = ..., /) -> tuple[str | None, tuple[Symtab_and_line, ...] | None]: ...
def prompt_hook(current_prompt: str) -> str: ...
def architecture_names() -> list[str]: ...
def connections() -> list[TargetConnection]: ...

# Exceptions

class error(RuntimeError): ...
class MemoryError(error): ...
class GdbError(Exception): ...

# Values

_ValueOrNative: TypeAlias = bool | float | str | Value
_ValueOrInt: TypeAlias = Value | int

class Value:
    address: Value
    is_optimized_out: bool
    type: Type
    dynamic_type: Type
    is_lazy: bool

    def __index__(self) -> int: ...
    def __int__(self) -> int: ...
    def __float__(self) -> float: ...
    def __add__(self, other: _ValueOrInt) -> Value: ...
    def __sub__(self, other: _ValueOrInt) -> Value: ...
    def __mul__(self, other: _ValueOrInt) -> Value: ...
    def __truediv__(self, other: _ValueOrInt) -> Value: ...
    def __mod__(self, other: _ValueOrInt) -> Value: ...
    def __and__(self, other: _ValueOrInt) -> Value: ...
    def __or__(self, other: _ValueOrInt) -> Value: ...
    def __xor__(self, other: _ValueOrInt) -> Value: ...
    def __lshift__(self, other: _ValueOrInt) -> Value: ...
    def __rshift__(self, other: _ValueOrInt) -> Value: ...
    def __eq__(self, other: _ValueOrInt) -> bool: ...  # type: ignore[override]
    def __ne__(self, other: _ValueOrInt) -> bool: ...  # type: ignore[override]
    def __lt__(self, other: _ValueOrInt) -> bool: ...
    def __le__(self, other: _ValueOrInt) -> bool: ...
    def __gt__(self, other: _ValueOrInt) -> bool: ...
    def __ge__(self, other: _ValueOrInt) -> bool: ...
    def __getitem__(self, key: int | str | Field) -> Value: ...
    def __call__(self, *args: _ValueOrNative) -> Value: ...
    def __init__(self, val: _ValueOrNative) -> None: ...
    def cast(self, type: Type) -> Value: ...
    def dereference(self) -> Value: ...
    def referenced_value(self) -> Value: ...
    def reference_value(self) -> Value: ...
    def const_value(self) -> Value: ...
    def dynamic_cast(self, type: Type) -> Value: ...
    def reinterpret_cast(self, type: Type) -> Value: ...
    def format_string(
        self,
        raw: bool = ...,
        pretty_arrays: bool = ...,
        pretty_structs: bool = ...,
        array_indexes: bool = ...,
        symbols: bool = ...,
        unions: bool = ...,
        address: bool = ...,
        deref_refs: bool = ...,
        actual_objects: bool = ...,
        static_members: bool = ...,
        max_elements: int = ...,
        max_depth: int = ...,
        repeat_threshold: int = ...,
        format: str = ...,
    ) -> str: ...
    def string(self, encoding: str = ..., errors: str = ..., length: int = ...) -> str: ...
    def lazy_string(self, encoding: str = ..., length: int = ...) -> LazyString: ...
    def fetch_lazy(self) -> None: ...

# Types

def lookup_type(name: str, block: Block = ...) -> Type: ...
@final
class Type:
    alignof: int
    code: int
    dynamic: bool
    name: str
    sizeof: int
    tag: str | None
    objfile: Objfile | None

    def fields(self) -> list[Field]: ...
    def array(self, n1: int | Value, n2: int | Value = ...) -> Type: ...
    def vector(self, n1: int, n2: int = ...) -> Type: ...
    def const(self) -> Type: ...
    def volatile(self) -> Type: ...
    def unqualified(self) -> Type: ...
    def range(self) -> tuple[int, int]: ...
    def reference(self) -> Type: ...
    def pointer(self) -> Type: ...
    def strip_typedefs(self) -> Type: ...
    def target(self) -> Type: ...
    def template_argument(self, n: int, block: Block = ...) -> Type: ...
    def optimized_out(self) -> Value: ...

@final
class Field:
    bitpos: int
    enumval: int
    name: str | None
    artificial: bool
    is_base_class: bool
    bitsize: int
    type: Type
    parent_type: Type

TYPE_CODE_PTR: int
TYPE_CODE_ARRAY: int
TYPE_CODE_STRUCT: int
TYPE_CODE_UNION: int
TYPE_CODE_ENUM: int
TYPE_CODE_FLAGS: int
TYPE_CODE_FUNC: int
TYPE_CODE_INT: int
TYPE_CODE_FLT: int
TYPE_CODE_VOID: int
TYPE_CODE_SET: int
TYPE_CODE_RANGE: int
TYPE_CODE_STRING: int
TYPE_CODE_BITSTRING: int
TYPE_CODE_ERROR: int
TYPE_CODE_METHOD: int
TYPE_CODE_METHODPTR: int
TYPE_CODE_MEMBERPTR: int
TYPE_CODE_REF: int
TYPE_CODE_RVALUE_REF: int
TYPE_CODE_CHAR: int
TYPE_CODE_BOOL: int
TYPE_CODE_COMPLEX: int
TYPE_CODE_TYPEDEF: int
TYPE_CODE_NAMESPACE: int
TYPE_CODE_DECFLOAT: int
TYPE_CODE_INTERNAL_FUNCTION: int

# Pretty Printing

class _PrettyPrinter(Protocol):
    # TODO: The "children" and "display_hint" methods are optional for
    # pretty-printers. Unfortunately, there is no such thing as an optional
    # method in the type system at the moment.
    #
    # def children(self) -> Iterator[tuple[str, _ValueOrNative]]: ...
    # def display_hint(self) -> str | None: ...
    def to_string(self) -> str | LazyString: ...

_PrettyPrinterLookupFunction: TypeAlias = Callable[[Value], _PrettyPrinter | None]

def default_visualizer(value: Value, /) -> _PrettyPrinter | None: ...

# Selecting Pretty-Printers

pretty_printers: list[_PrettyPrinterLookupFunction]

# Filtering Frames

class _FrameFilter(Protocol):
    name: str
    enabled: bool
    priority: int

    def filter(self, iterator: Iterator[_FrameDecorator]) -> Iterator[_FrameDecorator]: ...

# Decorating Frames

class _SymValueWrapper(Protocol):
    def symbol(self) -> Symbol | str: ...
    def value(self) -> _ValueOrNative | None: ...

class _FrameDecorator(Protocol):
    def elided(self) -> Iterator[Frame] | None: ...
    def function(self) -> str | None: ...
    def address(self) -> int | None: ...
    def filename(self) -> str | None: ...
    def line(self) -> int | None: ...
    def frame_args(self) -> Iterator[_SymValueWrapper] | None: ...
    def frame_locals(self) -> Iterator[_SymValueWrapper] | None: ...
    def inferior_frame(self) -> Frame: ...

# Unwinding Frames

@final
class PendingFrame:
    def read_register(self, reg: str | RegisterDescriptor | int, /) -> Value: ...
    def create_unwind_info(self, frame_id: object, /) -> UnwindInfo: ...
    def architecture(self) -> Architecture: ...
    def level(self) -> int: ...

class UnwindInfo:
    def add_saved_register(self, reg: str | RegisterDescriptor | int, value: Value, /) -> None: ...

class Unwinder:
    name: str
    enabled: bool

    def __call__(self, pending_frame: Frame) -> UnwindInfo | None: ...

# Xmethods: the API is defined in the "xmethod" module

# Inferiors

def inferiors() -> tuple[Inferior, ...]: ...
def selected_inferior() -> Inferior: ...

_BufferType: TypeAlias = _typeshed.ReadableBuffer

@final
class Inferior:
    num: int
    connection_num: int
    pid: int
    was_attached: bool
    progspace: Progspace

    def is_valid(self) -> bool: ...
    def threads(self) -> tuple[InferiorThread, ...]: ...
    def architecture(self) -> Architecture: ...
    def read_memory(self, address: _ValueOrInt, length: int) -> memoryview: ...
    def write_memory(self, address: _ValueOrInt, buffer: _BufferType, length: int = ...) -> memoryview: ...
    def search_memory(self, address: _ValueOrInt, length: int, pattern: _BufferType) -> int | None: ...
    def thread_from_handle(self, handle: Value) -> InferiorThread: ...

# Threads

def selected_thread() -> InferiorThread: ...
@final
class InferiorThread:
    name: str | None
    num: int
    global_num: int
    ptid: tuple[int, int, int]
    inferior: Inferior

    def is_valid(self) -> bool: ...
    def switch(self) -> None: ...
    def is_stopped(self) -> bool: ...
    def is_running(self) -> bool: ...
    def is_exited(self) -> bool: ...
    def handle(self) -> bytes: ...

# Recordings

def start_recording(method: str = ..., format: str = ..., /) -> Record: ...
def current_recording() -> Record | None: ...
def stop_recording() -> None: ...

class Record:
    method: str
    format: str | None
    begin: Instruction
    end: Instruction
    replay_position: Instruction | None
    instruction_history: list[Instruction]
    function_call_history: list[RecordFunctionSegment]

    def goto(self, instruction: Instruction, /) -> None: ...

class Instruction:
    pc: int
    data: memoryview
    decoded: str
    size: int

class RecordInstruction(Instruction):
    number: int
    sal: Symtab_and_line | None
    is_speculative: bool

class RecordGap(Instruction):
    number: int
    error_code: int
    error_string: str

class RecordFunctionSegment:
    number: int
    symbol: Symbol | None
    level: int | None
    instructions: list[RecordInstruction | RecordGap]
    up: RecordFunctionSegment | None
    prev: RecordFunctionSegment | None
    next: RecordFunctionSegment | None

# CLI Commands

class Command:
    def __init__(self, name: str, command_class: int, completer_class: int = ..., prefix: bool = ...) -> None: ...
    def dont_repeat(self) -> None: ...
    def invoke(self, argument: str, from_tty: bool) -> None: ...
    def complete(self, text: str, word: str) -> object: ...

def string_to_argv(argv: str, /) -> list[str]: ...

COMMAND_NONE: int
COMMAND_RUNNING: int
COMMAND_DATA: int
COMMAND_STACK: int
COMMAND_FILES: int
COMMAND_SUPPORT: int
COMMAND_STATUS: int
COMMAND_BREAKPOINTS: int
COMMAND_TRACEPOINTS: int
COMMAND_TUI: int
COMMAND_USER: int
COMMAND_OBSCURE: int
COMMAND_MAINTENANCE: int

COMPLETE_NONE: int
COMPLETE_FILENAME: int
COMPLETE_LOCATION: int
COMPLETE_COMMAND: int
COMPLETE_SYMBOL: int
COMPLETE_EXPRESSION: int

# GDB/MI Commands

class MICommand:
    name: str
    installed: bool

    def __init__(self, name: str) -> None: ...
    def invoke(self, arguments: list[str]) -> dict[str, object] | None: ...

# Parameters

class Parameter:
    set_doc: str
    show_doc: str
    value: object

    def __init__(self, name: str, command_class: int, parameter_class: int, enum_sequence: Sequence[str] = ...) -> None: ...
    def get_set_string(self) -> str: ...
    def get_show_string(self, svalue: str) -> str: ...

PARAM_BOOLEAN: int
PARAM_AUTO_BOOLEAN: int
PARAM_UINTEGER: int
PARAM_INTEGER: int
PARAM_STRING: int
PARAM_STRING_NOESCAPE: int
PARAM_OPTIONAL_FILENAME: int
PARAM_FILENAME: int
PARAM_ZINTEGER: int
PARAM_ZUINTEGER: int
PARAM_ZUINTEGER_UNLIMITED: int
PARAM_ENUM: int

# Convenience functions

class Function:
    def __init__(self, name: str) -> None: ...
    def invoke(self, *args: Value) -> _ValueOrNative: ...

# Progspaces

def current_progspace() -> Progspace | None: ...
def progspaces() -> Sequence[Progspace]: ...
@final
class Progspace:
    filename: str
    pretty_printers: list[_PrettyPrinterLookupFunction]
    type_printers: list[gdb.types._TypePrinter]
    frame_filters: list[_FrameFilter]

    def block_for_pc(self, pc: int, /) -> Block | None: ...
    def find_pc_line(self, pc: int, /) -> Symtab_and_line: ...
    def is_valid(self) -> bool: ...
    def objfiles(self) -> Sequence[Objfile]: ...
    def solib_name(self, address: int, /) -> str | None: ...

# Objfiles

def current_objfile() -> Objfile | None: ...
def objfiles() -> list[Objfile]: ...
def lookup_objfile(name: str, by_build_id: bool = ...) -> Objfile | None: ...
@final
class Objfile:
    filename: str | None
    username: str | None
    owner: Objfile | None
    build_id: str | None
    progspace: Progspace
    pretty_printers: list[_PrettyPrinterLookupFunction]
    type_printers: list[gdb.types._TypePrinter]
    frame_filters: list[_FrameFilter]

    def is_valid(self) -> bool: ...
    def add_separate_debug_file(self, file: str) -> None: ...
    def lookup_global_symbol(self, name: str, domain: int = ...) -> Symbol | None: ...
    def lookup_static_method(self, name: str, domain: int = ...) -> Symbol | None: ...

# Frames

def selected_frame() -> Frame: ...
def newest_frame() -> Frame: ...
def frame_stop_reason_string(code: int, /) -> str: ...
def invalidate_cached_frames() -> None: ...

NORMAL_FRAME: int
INLINE_FRAME: int
TAILCALL_FRAME: int
SIGTRAMP_FRAME: int
ARCH_FRAME: int
SENTINEL_FRAME: int

FRAME_UNWIND_NO_REASON: int
FRAME_UNWIND_NULL_ID: int
FRAME_UNWIND_OUTERMOST: int
FRAME_UNWIND_UNAVAILABLE: int
FRAME_UNWIND_INNER_ID: int
FRAME_UNWIND_SAME_ID: int
FRAME_UNWIND_NO_SAVED_PC: int
FRAME_UNWIND_MEMORY_ERROR: int
FRAME_UNWIND_FIRST_ERROR: int

@final
class Frame:
    def is_valid(self) -> bool: ...
    def name(self) -> str | None: ...
    def architecture(self) -> Architecture: ...
    def type(self) -> int: ...
    def unwind_stop_reason(self) -> int: ...
    def pc(self) -> Value: ...
    def block(self) -> Block: ...
    def function(self) -> Symbol: ...
    def older(self) -> Frame | None: ...
    def newer(self) -> Frame | None: ...
    def find_sal(self) -> Symtab_and_line: ...
    def read_register(self, register: str | RegisterDescriptor | int, /) -> Value: ...
    def read_var(self, variable: str | Symbol, /, block: Block | None = ...) -> Value: ...
    def select(self) -> None: ...
    def level(self) -> int: ...

# Blocks

def block_for_pc(pc: int) -> Block | None: ...
@final
class Block:
    start: int
    end: int
    function: Symbol | None
    superblock: Block | None
    global_block: Block
    static_block: Block | None
    is_global: bool
    is_static: bool

    def is_valid(self) -> bool: ...
    def __iter__(self) -> BlockIterator: ...

@final
class BlockIterator:
    def is_valid(self) -> bool: ...
    def __iter__(self: _typeshed.Self) -> _typeshed.Self: ...
    def __next__(self) -> Symbol: ...

# Symbols

def lookup_symbol(name: str, block: Block | None = ..., domain: int = ...) -> tuple[Symbol | None, bool]: ...
def lookup_global_symbol(name: str, domain: int = ...) -> Symbol | None: ...
def lookup_static_symbol(name: str, domain: int = ...) -> Symbol | None: ...
def lookup_static_symbols(name: str, domain: int = ...) -> list[Symbol]: ...
@final
class Symbol:
    type: Type | None
    symtab: Symtab
    line: int
    name: str
    linkage_name: str
    print_name: str
    addr_class: int
    needs_frame: bool
    is_argument: bool
    is_constant: bool
    is_function: bool
    is_variable: bool

    def is_valid(self) -> bool: ...
    def value(self, frame: Frame = ..., /) -> Value: ...

SYMBOL_UNDEF_DOMAIN: int
SYMBOL_VAR_DOMAIN: int
SYMBOL_STRUCT_DOMAIN: int
SYMBOL_LABEL_DOMAIN: int
SYMBOL_MODULE_DOMAIN: int
SYMBOL_COMMON_BLOCK_DOMAIN: int

SYMBOL_LOC_UNDEF: int
SYMBOL_LOC_CONST: int
SYMBOL_LOC_STATIC: int
SYMBOL_LOC_REGISTER: int
SYMBOL_LOC_ARG: int
SYMBOL_LOC_REF_ARG: int
SYMBOL_LOC_REGPARM_ADDR: int
SYMBOL_LOC_LOCAL: int
SYMBOL_LOC_TYPEDEF: int
SYMBOL_LOC_LABEL: int
SYMBOL_LOC_BLOCK: int
SYMBOL_LOC_CONST_BYTES: int
SYMBOL_LOC_UNRESOLVED: int
SYMBOL_LOC_OPTIMIZED_OUT: int
SYMBOL_LOC_COMPUTED: int
SYMBOL_LOC_COMMON_BLOCK: int

# Symbol tables

@final
class Symtab_and_line:
    symtab: Symtab
    pc: int
    last: int
    line: int

    def is_valid(self) -> bool: ...

@final
class Symtab:
    filename: str
    objfile: Objfile
    producer: str

    def is_valid(self) -> bool: ...
    def fullname(self) -> str: ...
    def global_block(self) -> Block: ...
    def static_block(self) -> Block: ...
    def linetable(self) -> LineTable: ...

# Line Tables

@final
class LineTableEntry:
    line: int
    pc: int

@final
class LineTable(Iterator[LineTableEntry]):
    def __iter__(self: _typeshed.Self) -> _typeshed.Self: ...
    def __next__(self) -> LineTableEntry: ...
    def line(self, line: int, /) -> tuple[LineTableEntry, ...]: ...
    def has_line(self, line: int, /) -> bool: ...
    def source_lnes(self) -> list[int]: ...

# Breakpoints

class Breakpoint:
    @overload
    def __init__(
        self, spec: str, type: int = ..., wp_class: int = ..., internal: bool = ..., temporary: bool = ..., qualified: bool = ...
    ) -> None: ...
    @overload
    def __init__(
        self,
        source: str = ...,
        function: str = ...,
        label: str = ...,
        line: int = ...,
        internal: bool = ...,
        temporary: bool = ...,
        qualified: bool = ...,
    ) -> None: ...
    def stop(self) -> bool: ...
    def is_valid(self) -> bool: ...
    def delete(self) -> None: ...

    enabled: bool
    silent: bool
    pending: bool
    thread: int | None
    task: str | None
    ignore_count: int
    number: int
    type: int
    visible: bool
    temporary: bool
    hit_count: int
    location: str | None
    expression: str | None
    condition: str | None
    commands: str | None

BP_BREAKPOINT: int
BP_HARDWARE_BREAKPOINT: int
BP_WATCHPOINT: int
BP_HARDWARE_WATCHPOINT: int
BP_READ_WATCHPOINT: int
BP_ACCESS_WATCHPOINT: int
BP_CATCHPOINT: int

WP_READ: int
WP_WRITE: int
WP_ACCESS: int

# Finish Breakpoints

class FinishBreakpoint(Breakpoint):
    return_value: Value | None

    def __init__(self, frame: Frame = ..., internal: bool = ...) -> None: ...
    def out_of_scope(self) -> None: ...

# Lazy strings

class LazyString:
    def value(self) -> Value: ...

    address: Value
    length: int
    encoding: str
    type: Type

# Architectures

@final
class Architecture:
    def name(self) -> str: ...
    def disassemble(self, start_pc: int, end_pc: int = ..., count: int = ...) -> list[dict[str, object]]: ...
    def integer_type(self, size: int, signed: bool = ...) -> Type: ...
    def registers(self, reggroup: str = ...) -> RegisterDescriptorIterator: ...
    def register_groups(self) -> RegisterGroupsIterator: ...

# Registers

class RegisterDescriptor:
    name: str

class RegisterDescriptorIterator(Iterator[RegisterDescriptor]):
    def __next__(self) -> RegisterDescriptor: ...
    def find(self, name: str) -> RegisterDescriptor | None: ...

class RegisterGroup:
    name: str

class RegisterGroupsIterator(Iterator[RegisterGroup]):
    def __next__(self) -> RegisterGroup: ...

# Connections

class TargetConnection:
    def is_valid(self) -> bool: ...

    num: int
    type: str
    description: str
    details: str | None

class RemoteTargetConnection(TargetConnection):
    def send_packet(self, packet: str | bytes) -> bytes: ...

# TUI Windows

def register_window_type(name: str, factory: Callable[[TuiWindow], _Window]) -> None: ...

class TuiWindow:
    width: int
    height: int
    title: str

    def is_valid(self) -> bool: ...
    def erase(self) -> None: ...
    def write(self, string: str, full_window: bool = ..., /) -> None: ...

class _Window(Protocol):
    def close(self) -> None: ...
    def render(self) -> None: ...
    def hscroll(self, num: int) -> None: ...
    def vscroll(self, num: int) -> None: ...
    def click(self, x: int, y: int, button: int) -> None: ...
