# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import codecs
import re
import sys
from dataclasses import dataclass, field, fields
from enum import Enum
from typing import Any, Callable, FrozenSet, List, Mapping, Optional, Pattern, Union

from libcst._add_slots import add_slots
from libcst._nodes.whitespace import NEWLINE_RE
from libcst._parser.parso.utils import parse_version_string, PythonVersionInfo

_INDENT_RE: Pattern[str] = re.compile(r"[ \t]+")

try:
    from libcst_native import parser_config as config_mod

    MockWhitespaceParserConfig = config_mod.BaseWhitespaceParserConfig
except ImportError:
    from libcst._parser.types import py_config as config_mod

    MockWhitespaceParserConfig = config_mod.MockWhitespaceParserConfig

BaseWhitespaceParserConfig = config_mod.BaseWhitespaceParserConfig
ParserConfig = config_mod.ParserConfig
parser_config_asdict: Callable[[ParserConfig], Mapping[str, Any]] = (
    config_mod.parser_config_asdict
)


class AutoConfig(Enum):
    """
    A sentinel value used in PartialParserConfig
    """

    token: int = 0

    def __repr__(self) -> str:
        return str(self)


# This list should be kept in sorted order.
KNOWN_PYTHON_VERSION_STRINGS = ["3.0", "3.1", "3.3", "3.5", "3.6", "3.7", "3.8"]


@add_slots
@dataclass(frozen=True)
class PartialParserConfig:
    r"""
    An optional object that can be supplied to the parser entrypoints (e.g.
    :func:`parse_module`) to configure the parser.

    Unspecified fields will be inferred from the input source code or from the execution
    environment.

    >>> import libcst as cst
    >>> tree = cst.parse_module("abc")
    >>> tree.bytes
    b'abc'
    >>> # override the default utf-8 encoding
    ... tree = cst.parse_module("abc", cst.PartialParserConfig(encoding="utf-32"))
    >>> tree.bytes
    b'\xff\xfe\x00\x00a\x00\x00\x00b\x00\x00\x00c\x00\x00\x00'
    """

    #: The version of Python that the input source code is expected to be syntactically
    #: compatible with. This may be different from the Python interpreter being used to
    #: run LibCST. For example, you can parse code as 3.7 with a CPython 3.6
    #: interpreter.
    #:
    #: If unspecified, it will default to the syntax of the running interpreter
    #: (rounding down from among the following list).
    #:
    #: Currently, only Python 3.0, 3.1, 3.3, 3.5, 3.6, 3.7 and 3.8 syntax is supported.
    #: The gaps did not have any syntax changes from the version prior.
    python_version: Union[str, AutoConfig] = AutoConfig.token

    #: A named tuple with the ``major`` and ``minor`` Python version numbers. This is
    #: derived from :attr:`python_version` and should not be supplied to the
    #: :class:`PartialParserConfig` constructor.
    parsed_python_version: PythonVersionInfo = field(init=False)

    #: The file's encoding format. When parsing a ``bytes`` object, this value may be
    #: inferred from the contents of the parsed source code. When parsing a ``str``,
    #: this value defaults to ``"utf-8"``.
    encoding: Union[str, AutoConfig] = AutoConfig.token

    #: Detected ``__future__`` import names
    future_imports: Union[FrozenSet[str], AutoConfig] = AutoConfig.token

    #: The indentation of the file, expressed as a series of tabs and/or spaces. This
    #: value is inferred from the contents of the parsed source code by default.
    default_indent: Union[str, AutoConfig] = AutoConfig.token

    #: The newline of the file, expressed as ``\n``, ``\r\n``, or ``\r``. This value is
    #: inferred from the contents of the parsed source code by default.
    default_newline: Union[str, AutoConfig] = AutoConfig.token

    def __post_init__(self) -> None:
        raw_python_version = self.python_version

        if isinstance(raw_python_version, AutoConfig):
            # If unspecified, we'll try to pick the same as the running
            # interpreter.  There will always be at least one entry.
            parsed_python_version = _pick_compatible_python_version()
        else:
            # If the caller specified a version, we require that to be a known
            # version (because we don't want to encourage doing duplicate work
            # when there weren't syntax changes).

            # `parse_version_string` will raise a ValueError if the version is
            # invalid.
            parsed_python_version = parse_version_string(raw_python_version)

        if not any(
            parsed_python_version == parse_version_string(v)
            for v in KNOWN_PYTHON_VERSION_STRINGS
        ):
            comma_versions = ", ".join(KNOWN_PYTHON_VERSION_STRINGS)
            raise ValueError(
                "LibCST can only parse code using one of the following versions of "
                + f"Python's grammar: {comma_versions}. More versions may be "
                + "supported by future releases."
            )

        # We use object.__setattr__ because the dataclass is frozen. See:
        # https://docs.python.org/3/library/dataclasses.html#frozen-instances
        # This should be safe behavior inside of `__post_init__`.
        object.__setattr__(self, "parsed_python_version", parsed_python_version)

        encoding = self.encoding
        if not isinstance(encoding, AutoConfig):
            try:
                codecs.lookup(encoding)
            except LookupError:
                raise ValueError(f"{repr(encoding)} is not a supported encoding")

        newline = self.default_newline
        if (
            not isinstance(newline, AutoConfig)
            and NEWLINE_RE.fullmatch(newline) is None
        ):
            raise ValueError(
                f"Got an invalid value for default_newline: {repr(newline)}"
            )

        indent = self.default_indent
        if not isinstance(indent, AutoConfig) and _INDENT_RE.fullmatch(indent) is None:
            raise ValueError(f"Got an invalid value for default_indent: {repr(indent)}")

    def __repr__(self) -> str:
        init_keys: List[str] = []

        for f in fields(self):
            # We don't display the parsed_python_version attribute because it contains
            # the same value as python_version, only parsed.
            if f.name == "parsed_python_version":
                continue
            value = getattr(self, f.name)
            if not isinstance(value, AutoConfig):
                init_keys.append(f"{f.name}={value!r}")

        return f"{self.__class__.__name__}({', '.join(init_keys)})"


def _pick_compatible_python_version(version: Optional[str] = None) -> PythonVersionInfo:
    max_version = parse_version_string(version)
    for v in KNOWN_PYTHON_VERSION_STRINGS[::-1]:
        tmp = parse_version_string(v)
        if tmp <= max_version:
            return tmp

    raise ValueError(
        f"No version found older than {version} ({max_version}) while "
        + f"running on {sys.version_info}"
    )
