# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Basic datatypes for parsed pyc files."""

from dataclasses import dataclass

from typing import Any, List, Optional, Tuple, Union


@dataclass
class CodeTypeBase:
    """Pure python types.CodeType with python version added."""

    python_version: Tuple[int, int]
    co_argcount: int
    co_posonlyargcount: int
    co_kwonlyargcount: int
    co_stacksize: int
    co_flags: int
    co_code: bytes
    co_consts: List[object]
    co_names: List[str]
    co_filename: Union[bytes, str]
    co_name: str
    co_firstlineno: int

    def __repr__(self):
        return f"<code: {self.co_name}>"


@dataclass
class CodeType38(CodeTypeBase):
    """CodeType for python 3.8 - 3.10."""

    co_nlocals: int
    co_lnotab: bytes
    co_varnames: List[str]
    co_freevars: Tuple[str, ...]
    co_cellvars: Tuple[str, ...]


@dataclass
class CodeType311(CodeTypeBase):
    """CodeType for python 3.11+."""

    co_qualname: str
    co_localsplusnames: Tuple[str, ...]
    co_localspluskinds: Tuple[int, ...]
    co_linetable: bytes
    co_exceptiontable: bytes


@dataclass
class Opcode:
    """Opcode with names and line numbers."""

    offset: int
    line: int
    endline: Optional[int]
    col: Optional[int]
    endcol: Optional[int]
    op: int
    name: str
    arg: Optional[int]
    argval: Any

    def __str__(self):
        ret = f"{self.line:>5}{self.offset:>6}  {self.name:<30}"
        if self.arg is not None:
            ret += f" {self.arg:>5} {self.argval}"
        return ret


@dataclass
class ExceptionTableEntry:
    """Exception table entry in python 3.11+."""

    start: int
    end: int
    target: int
    depth: int
    lasti: bool

    def pretty_format(self):
        return (
            f"{self.start} to {self.end} -> {self.target} "
            f"[{self.depth}] {'lasti' if self.lasti else ''}"
        )


@dataclass
class ExceptionTable:
    """Exception table in python 3.11+."""

    entries: List[ExceptionTableEntry]

    def __bool__(self):
        return bool(self.entries)


@dataclass
class DisassembledCode:
    """Tree of bytecode and associated opcode list."""

    code: CodeTypeBase
    opcodes: List[Opcode]
    exception_table: ExceptionTable
    children: "List[DisassembledCode]"

    @property
    def name(self):
        return self.code.co_name

    @property
    def python_version(self):
        return self.code.python_version

    def get_child(self, name) -> "Optional[DisassembledCode]":
        """Get the first child with name = `name`."""
        for c in self.children:
            if c.name == name:
                return c

    def pretty_format(self, indent=0):
        header = (
            f"-- <code object {self.name}>, file: {self.code.co_filename}, "
            f"line: {self.code.co_firstlineno}"
        )
        out = [(indent, header)]
        for op in self.opcodes:
            out.append((indent, str(op)))
        if self.exception_table:
            out.append((indent, "ExceptionTable:"))
            for e in self.exception_table.entries:
                out.append((indent + 2, e.pretty_format()))
        for c in self.children:
            out.append((0, ""))
            out.extend(c.pretty_format(indent + 2))
        return out

    def pretty_print(self, indent=0):
        lines = self.pretty_format(indent)
        for i, text in lines:
            print(" " * i, text)
