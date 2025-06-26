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

"""Bytecode reader."""

from dataclasses import dataclass

from typing import cast, Iterable, List, Optional

from . import linetable
from . import mapping
from . import types


# From cpython/Include/opcodes.h
HAVE_ARGUMENT = 90
EXTENDED_ARG = 144


@dataclass
class RawOpcode:
    """Opcode parsed from code.co_code."""

    start: int
    end: int
    op: int
    arg: Optional[int]


def wordcode_reader(data: bytes) -> Iterable[RawOpcode]:
    """Reads binary data from pyc files."""

    extended_arg = 0
    start = 0
    for pos in range(0, len(data), 2):
        op = data[pos]
        if op == EXTENDED_ARG:
            oparg = data[pos + 1] | extended_arg
            extended_arg = oparg << 8
        elif op >= HAVE_ARGUMENT:
            oparg = data[pos + 1] | extended_arg
            extended_arg = 0
        else:
            oparg = None
            extended_arg = 0
        # Don't yield EXTENDED_ARG; it is part of the next opcode.
        if op != EXTENDED_ARG:
            yield RawOpcode(start, pos + 2, op, oparg)
            start = pos + 2


# In python 3.11+ attributes like co_consts can be None, so following
# cpython/Lib/dis.py we define an UNKNOWN value for args whose value we cannot
# calculate.
class _Unknown:
    def __repr__(self):
        return "<unknown>"


# Sentinel to represent values that cannot be calculated
UNKNOWN = _Unknown()


def _is_backward_jump(name):
    return "JUMP_BACKWARD" in name


class Disassembler:
    """Disassemble code."""

    def __init__(self, code):
        self.code = code
        self.python_version = code.python_version[:2]
        self.opmap = mapping.get_mapping(self.python_version)
        if self.python_version < (3, 11):
            code = cast(types.CodeType38, code)
            self.cell_names = code.co_cellvars + code.co_freevars

    def _lookup(self, vals: Optional[tuple], arg: int):
        if vals is None or arg >= len(vals):
            return UNKNOWN
        return vals[arg]

    def _decode_arg(self, name: str, arg_type: int, arg: int, end_pos: int):
        if self.python_version >= (3, 12):
            if name == "COMPARE_OP":
                return arg >> 4
            if name == "LOAD_ATTR":
                return arg >> 1
            if name == "LOAD_SUPER_ATTR":
                return arg >> 2
        if self.python_version >= (3, 11):
            if name == "LOAD_GLOBAL":
                return arg // 2
        if self.python_version >= (3, 10):
            if arg_type == mapping.JREL:
                signed_arg = -arg if _is_backward_jump(name) else arg
                target = signed_arg * 2 + end_pos
                if self.python_version >= (3, 12):
                    target += 2 * mapping.caches(name, self.python_version)
                return target
            elif arg_type == mapping.JABS:
                return arg * 2
        if arg_type == mapping.JREL:
            return arg + end_pos
        return arg

    def _get_argval(self, name: str, arg: int, end_pos: int):
        arg_type = mapping.arg_type(name, self.python_version)
        arg = self._decode_arg(name, arg_type, arg, end_pos)
        if arg_type == mapping.CONST:
            return self._lookup(self.code.co_consts, arg)
        elif arg_type == mapping.NAME:
            return self._lookup(self.code.co_names, arg)
        elif arg_type == mapping.LOCAL:
            if self.python_version >= (3, 11):
                return self._lookup(self.code.co_localsplusnames, arg)
            else:
                return self._lookup(self.code.co_varnames, arg)
        elif arg_type == mapping.FREE:
            if self.python_version >= (3, 11):
                return self._lookup(self.code.co_localsplusnames, arg)
            else:
                return self._lookup(self.cell_names, arg)
        elif name == "CALL_INTRINSIC_1":
            return mapping.intrinsic_1_desc(arg, self.python_version)
        elif name == "CALL_INTRINSIC_2":
            return mapping.intrinsic_2_desc(arg, self.python_version)
        else:
            return arg

    def dis(self) -> List[types.Opcode]:
        """Disassemble code."""
        lt = linetable.linetable_reader(self.code)
        ret = []
        for o in wordcode_reader(self.code.co_code):
            if o.op == 0:  # CACHE
                continue
            name = self.opmap[o.op]
            pos = lt.get(o.start)
            argval = self._get_argval(name, o.arg, o.end)
            if isinstance(argval, types.CodeTypeBase):
                argval = f"<code:{argval.co_name}>"
            op = types.Opcode(
                offset=o.start,
                name=name,
                line=pos.line,
                endline=pos.endline,
                col=pos.startcol,
                endcol=pos.endcol,
                op=o.op,
                arg=o.arg,
                argval=argval,
            )
            ret.append(op)
        return ret


def dis(code: types.CodeTypeBase) -> List[types.Opcode]:
    """Disassemble a single piece of top-level code."""
    return Disassembler(code).dis()


def dis_all(code: types.CodeTypeBase) -> types.DisassembledCode:
    """Recursively disassemble code and contained code blocks."""
    opcodes = dis(code)
    if code.python_version >= (3, 11):
        code = cast(types.CodeType311, code)
        et = linetable.ExceptionTableReader(code)
        exc_table = types.ExceptionTable(et.read_all())
    else:
        # Setting this to None will complicate typechecking without adding any
        # real safety; client code will typically check for version >= 3.11
        # before accessing the exception table, rather than doing type or
        # presence checks on the attribute.
        exc_table = types.ExceptionTable([])
    ret = types.DisassembledCode(
        code=code, opcodes=opcodes, exception_table=exc_table, children=[]
    )
    for child in code.co_consts:
        if hasattr(child, "co_code"):
            ret.children.append(dis_all(child))
    return ret
