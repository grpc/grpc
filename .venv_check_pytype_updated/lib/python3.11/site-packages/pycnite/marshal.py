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

"""A pure python version of marshal.loads."""

import struct
import sys
from typing import Tuple

from . import types


class Type:
    """Type enum from marshal.c."""

    NULL = ord("0")
    NONE = ord("N")
    FALSE = ord("F")
    TRUE = ord("T")
    STOPITER = ord("S")
    ELLIPSIS = ord(".")
    INT = ord("i")
    INT64 = ord("I")
    FLOAT = ord("f")
    BINARY_FLOAT = ord("g")
    COMPLEX = ord("x")
    BINARY_COMPLEX = ord("y")
    LONG = ord("l")
    STRING = ord("s")
    STRINGREF = ord("R")
    INTERNED = ord("t")
    REF = ord("r")
    TUPLE = ord("(")
    LIST = ord("[")
    DICT = ord("{")
    CODE = ord("c")
    UNICODE = ord("u")
    UNKNOWN = ord("?")
    SET = ord("<")
    FROZENSET = ord(">")
    ASCII = ord("a")
    ASCII_INTERNED = ord("A")
    SMALL_TUPLE = ord(")")
    SHORT_ASCII = ord("z")
    SHORT_ASCII_INTERNED = ord("Z")


class Flags:
    """Masks and values used by FORMAT_VALUE opcode."""

    FVC_MASK = 0x3
    FVC_NONE = 0x0
    FVC_STR = 0x1
    FVC_REPR = 0x2
    FVC_ASCII = 0x3
    FVS_MASK = 0x4
    FVS_HAVE_SPEC = 0x4

    # Flag used by CALL_FUNCTION_EX
    CALL_FUNCTION_EX_HAS_KWARGS = 0x1

    # Flags used by MAKE_FUNCTION
    MAKE_FUNCTION_HAS_POS_DEFAULTS = 0x1
    MAKE_FUNCTION_HAS_KW_DEFAULTS = 0x2
    MAKE_FUNCTION_HAS_ANNOTATIONS = 0x4
    MAKE_FUNCTION_HAS_FREE_VARS = 0x8

    # Or-ing this flag to one of the codes above will cause the decoded value to
    # be stored in a reference table for later lookup.
    REF = 0x80

    # Cell kinds
    CO_FAST_LOCAL = 0x20
    CO_FAST_CELL = 0x40
    CO_FAST_FREE = 0x80

    # for co_flags:
    CO_OPTIMIZED = 0x0001
    CO_NEWLOCALS = 0x0002
    CO_VARARGS = 0x0004
    CO_VARKEYWORDS = 0x0008
    CO_NESTED = 0x0010
    CO_GENERATOR = 0x0020
    CO_NOFREE = 0x0040
    CO_COROUTINE = 0x0080
    CO_ITERABLE_COROUTINE = 0x0100
    CO_ASYNC_GENERATOR = 0x0200
    CO_FUTURE_DIVISION = 0x2000
    CO_FUTURE_ABSOLUTE_IMPORT = 0x4000
    CO_FUTURE_WITH_STATEMENT = 0x8000
    CO_FUTURE_PRINT_FUNCTION = 0x10000
    CO_FUTURE_UNICODE_LITERALS = 0x20000


NULL = object()  # sentinel marker


class MarshalReader:
    """Stateful loader for marshalled files."""

    def __init__(self, data: bytes, python_version: Tuple[int, int]):
        self.bufstr = data
        self.bufpos = 0
        self.python_version = python_version
        self.refs = []
        self._stringtable = []

    def eof(self):
        """Return True if we reached the end of the stream."""
        return self.bufpos == len(self.bufstr)

    def load(self):
        """Load an encoded Python data structure."""
        c = ord("?")  # make pylint happy
        try:
            c = self._read_byte()
            if c & Flags.REF:
                # This element might recursively contain other elements, which
                # themselves store things in the refs table. So we need to
                # determine the index position *before* reading the contents of
                # this element.
                idx = self._reserve_ref()
                result = MarshalReader._DISPATCH[c & ~Flags.REF](self)
                self.refs[idx] = result
            else:
                result = MarshalReader._DISPATCH[c](self)
            return result
        except KeyError as e:
            raise ValueError(f"bad marshal code: {chr(c)!r} ({c:02x})") from e
        except IndexError as e:
            raise EOFError() from e

    def _read(self, n):
        """Read n bytes as a string."""
        pos = self.bufpos
        self.bufpos += n
        if self.bufpos > len(self.bufstr):
            raise EOFError()
        return self.bufstr[pos : self.bufpos]

    def _read_byte(self):
        """Read an unsigned byte."""
        pos = self.bufpos
        self.bufpos += 1
        return self.bufstr[pos]

    def _read_short(self):
        """Read a signed 16 bit word."""
        lo = self._read_byte()
        hi = self._read_byte()
        x = lo | (hi << 8)
        if x & 0x8000:
            # sign extension
            x -= 0x10000
        return x

    def _read_long(self):
        """Read a signed 32 bit word."""
        b = self._read(4)
        x = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24
        if b[3] & 0x80 and x > 0:
            # sign extension
            x = -((1 << 32) - x)
            return int(x)
        else:
            return x

    def _read_long64(self):
        """Read a signed 64 bit integer."""
        b = self._read(8)
        x = (
            b[0]
            | b[1] << 8
            | b[2] << 16
            | b[3] << 24
            | b[4] << 32
            | b[5] << 40
            | b[6] << 48
            | b[7] << 56
        )
        if b[7] & 0x80 and x > 0:
            # sign extension
            x = -((1 << 64) - x)
        return x

    def _read_sized(self):
        """Read a size and a variable number of bytes."""
        n = self._read_long()
        return self._read(n)

    def _reserve_ref(self):
        """Reserve one entry in the reference table.

        This is done before reading an element, because reading an element and
        all its subelements might change the size of the reference table.

        Returns:
          Reserved index position in the reference table.
        """
        # See r_ref_reserve in Python-3.4/Python/marshal.c
        idx = len(self.refs)
        self.refs.append(None)
        return idx

    # pylint: disable=missing-docstring
    # This is a bunch of small methods with self-explanatory names.

    def load_null(self):
        return NULL

    def load_none(self):
        return None

    def load_true(self):
        return True

    def load_false(self):
        return False

    def load_stopiter(self):
        return StopIteration

    def load_ellipsis(self):
        return Ellipsis

    def load_int(self):
        return self._read_long()

    def load_int64(self):
        return self._read_long64()

    def load_long(self):
        """Load a variable length integer."""
        size = self._read_long()
        x = 0
        for i in range(abs(size)):
            d = self._read_short()
            x |= d << (i * 15)
        return x if size >= 0 else -x

    def load_float(self):
        n = self._read_byte()
        s = self._read(n)
        return float(s)

    def load_binary_float(self):
        binary = self._read(8)
        return struct.unpack("<d", binary)[0]

    def load_complex(self):
        real = self.load_float()
        imag = self.load_float()
        return complex(real, imag)

    def load_binary_complex(self):
        binary = self._read(16)
        return complex(*struct.unpack("dd", binary))

    def load_string(self):
        s = self._read_sized()
        return bytes(s)

    def load_interned(self):
        s = self._read_sized()
        ret = sys.intern(s.decode("utf-8", "strict"))
        self._stringtable.append(ret)
        return ret

    def load_stringref(self):
        n = self._read_long()
        return self._stringtable[n]

    def load_unicode(self):
        # We need to convert bytes to a unicode string.
        # We use the 'backslashreplace' error mode in order to handle non-utf8
        # backslash-escaped string literals correctly.
        s = self._read_sized()
        return s.decode("utf8", "backslashreplace")

    def load_ascii(self):
        s = self._read_sized()
        return s.decode("ascii")

    def load_short_ascii(self):
        n = self._read_byte()
        s = self._read(n)
        return s.decode("ascii")

    def load_list(self):
        n = self._read_long()
        return [self.load() for _ in range(n)]

    def load_tuple(self):
        return tuple(self.load_list())

    def load_small_tuple(self):
        n = self._read_byte()
        elts = [self.load() for _ in range(n)]
        return tuple(elts)

    def load_dict(self):
        d = {}
        while True:
            key = self.load()
            if key is NULL:
                break
            value = self.load()
            d[key] = value
        return d

    def load_set(self):
        return set(self.load_list())

    def load_frozenset(self):
        return frozenset(self.load_list())

    def load_ref(self):
        n = self._read_long()
        return self.refs[n]

    def load_code(self):
        if self.python_version < (3, 11):
            return self.load_code_3_8()
        else:
            return self.load_code_3_11()

    def load_code_3_8(self):
        """Load a Python code object."""
        argcount = self._read_long()
        posonlyargcount = self._read_long()
        kwonlyargcount = self._read_long()
        nlocals = self._read_long()
        stacksize = self._read_long()
        flags = self._read_long()
        code = self.load()
        consts = self.load()
        names = self.load()
        varnames = self.load()
        freevars = self.load()
        cellvars = self.load()
        filename = self.load()
        name = self.load()
        firstlineno = self._read_long()
        # lnotab, from
        # https://github.com/python/cpython/blob/master/Objects/lnotab_notes.txt:
        # 'an array of unsigned bytes disguised as a Python bytes object'.
        lnotab = self.load()
        return types.CodeType38(
            co_argcount=argcount,
            co_posonlyargcount=posonlyargcount,
            co_kwonlyargcount=kwonlyargcount,
            co_nlocals=nlocals,
            co_stacksize=stacksize,
            co_flags=flags,
            co_code=code,
            co_consts=consts,
            co_names=names,
            co_varnames=varnames,
            co_filename=filename,
            co_name=name,
            co_firstlineno=firstlineno,
            co_lnotab=lnotab,
            co_freevars=freevars,
            co_cellvars=cellvars,
            python_version=self.python_version,
        )

    def load_code_3_11(self):
        """Load a Python code object."""
        argcount = self._read_long()
        posonlyargcount = self._read_long()
        kwonlyargcount = self._read_long()
        stacksize = self._read_long()
        flags = self._read_long()
        code = self.load()
        consts = self.load()
        names = self.load()
        localsplusnames = self.load()
        localspluskinds = self.load()
        filename = self.load()
        name = self.load()
        qualname = self.load()
        firstlineno = self._read_long()
        linetable = self.load()
        exceptiontable = self.load()

        return types.CodeType311(
            co_argcount=argcount,
            co_posonlyargcount=posonlyargcount,
            co_kwonlyargcount=kwonlyargcount,
            co_stacksize=stacksize,
            co_flags=flags,
            co_code=code,
            co_consts=consts,
            co_names=names,
            co_localsplusnames=localsplusnames,
            co_localspluskinds=localspluskinds,
            co_filename=filename,
            co_name=name,
            co_qualname=qualname,
            co_firstlineno=firstlineno,
            co_linetable=linetable,
            co_exceptiontable=exceptiontable,
            python_version=self.python_version,
        )

    # pylint: enable=missing-docstring

    _DISPATCH = {
        Type.ASCII: load_ascii,
        Type.ASCII_INTERNED: load_ascii,
        Type.BINARY_COMPLEX: load_binary_complex,
        Type.BINARY_FLOAT: load_binary_float,
        Type.CODE: load_code,
        Type.COMPLEX: load_complex,
        Type.DICT: load_dict,
        Type.ELLIPSIS: load_ellipsis,
        Type.FALSE: load_false,
        Type.FLOAT: load_float,
        Type.FROZENSET: load_frozenset,
        Type.INT64: load_int64,
        Type.INT: load_int,
        Type.INTERNED: load_interned,
        Type.LIST: load_list,
        Type.LONG: load_long,
        Type.NONE: load_none,
        Type.NULL: load_null,
        Type.REF: load_ref,
        Type.SET: load_set,
        Type.SHORT_ASCII: load_short_ascii,
        Type.SHORT_ASCII_INTERNED: load_short_ascii,
        Type.SMALL_TUPLE: load_small_tuple,
        Type.STOPITER: load_stopiter,
        Type.STRING: load_string,
        Type.STRINGREF: load_stringref,
        Type.TRUE: load_true,
        Type.TUPLE: load_tuple,
        Type.UNICODE: load_unicode,
    }


def loads(data: bytes, python_version: Tuple[int, int]):
    um = MarshalReader(data, python_version)
    result = um.load()
    if not um.eof():
        leftover = um.bufstr[um.bufpos :]
        if len(leftover) > 80:
            raise BufferError(
                f"trailing bytes in marshal data ({um.bufpos}...)"
            )
        else:
            raise BufferError(
                f"trailing bytes in marshal data ({um.bufpos}...): {leftover}"
            )
    return result
