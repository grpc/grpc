import io
from _typeshed import Incomplete
from typing_extensions import TypeAlias

from .cffi_opcode import *

VERSION_BASE: int
VERSION_EMBEDDED: int
VERSION_CHAR16CHAR32: int
USE_LIMITED_API: Incomplete

class GlobalExpr:
    name: Incomplete
    address: Incomplete
    type_op: Incomplete
    size: Incomplete
    check_value: Incomplete
    def __init__(self, name, address, type_op, size: int = 0, check_value: int = 0) -> None: ...
    def as_c_expr(self): ...
    def as_python_expr(self): ...

class FieldExpr:
    name: Incomplete
    field_offset: Incomplete
    field_size: Incomplete
    fbitsize: Incomplete
    field_type_op: Incomplete
    def __init__(self, name, field_offset, field_size, fbitsize, field_type_op) -> None: ...
    def as_c_expr(self): ...
    def as_python_expr(self) -> None: ...
    def as_field_python_expr(self): ...

class StructUnionExpr:
    name: Incomplete
    type_index: Incomplete
    flags: Incomplete
    size: Incomplete
    alignment: Incomplete
    comment: Incomplete
    first_field_index: Incomplete
    c_fields: Incomplete
    def __init__(self, name, type_index, flags, size, alignment, comment, first_field_index, c_fields) -> None: ...
    def as_c_expr(self): ...
    def as_python_expr(self): ...

class EnumExpr:
    name: Incomplete
    type_index: Incomplete
    size: Incomplete
    signed: Incomplete
    allenums: Incomplete
    def __init__(self, name, type_index, size, signed, allenums) -> None: ...
    def as_c_expr(self): ...
    def as_python_expr(self): ...

class TypenameExpr:
    name: Incomplete
    type_index: Incomplete
    def __init__(self, name, type_index) -> None: ...
    def as_c_expr(self): ...
    def as_python_expr(self): ...

class Recompiler:
    ffi: Incomplete
    module_name: Incomplete
    target_is_python: Incomplete
    def __init__(self, ffi, module_name, target_is_python: bool = False) -> None: ...
    def needs_version(self, ver) -> None: ...
    cffi_types: Incomplete
    def collect_type_table(self): ...
    ALL_STEPS: Incomplete
    def collect_step_tables(self): ...
    def write_source_to_f(self, f, preamble) -> None: ...
    def write_c_source_to_f(self, f, preamble) -> None: ...
    def write_py_source_to_f(self, f) -> None: ...

NativeIO: TypeAlias = io.StringIO

def make_c_source(ffi, module_name, preamble, target_c_file, verbose: bool = False): ...
def make_py_source(ffi, module_name, target_py_file, verbose: bool = False): ...
def recompile(
    ffi,
    module_name,
    preamble,
    tmpdir: str = ".",
    call_c_compiler: bool = True,
    c_file: Incomplete | None = None,
    source_extension: str = ".c",
    extradir: Incomplete | None = None,
    compiler_verbose: int = 1,
    target: Incomplete | None = None,
    debug: Incomplete | None = None,
    **kwds,
): ...
