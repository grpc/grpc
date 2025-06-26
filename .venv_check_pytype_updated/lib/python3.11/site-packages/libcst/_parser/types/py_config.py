# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import abc
from dataclasses import asdict, dataclass
from typing import Any, FrozenSet, Mapping, Sequence

from libcst._parser.parso.utils import PythonVersionInfo


class BaseWhitespaceParserConfig(abc.ABC):
    """
    Represents the subset of ParserConfig that the whitespace parser requires. This
    makes calling the whitespace parser in tests with a mocked configuration easier.
    """

    lines: Sequence[str]
    default_newline: str


@dataclass(frozen=True)
class MockWhitespaceParserConfig(BaseWhitespaceParserConfig):
    """
    An internal type used by unit tests.
    """

    lines: Sequence[str]
    default_newline: str


@dataclass(frozen=True)
class ParserConfig(BaseWhitespaceParserConfig):
    """
    An internal configuration object that the python parser passes around. These
    values are global to the parsed code and should not change during the lifetime
    of the parser object.
    """

    lines: Sequence[str]
    encoding: str
    default_indent: str
    default_newline: str
    has_trailing_newline: bool
    version: PythonVersionInfo
    future_imports: FrozenSet[str]


def parser_config_asdict(config: ParserConfig) -> Mapping[str, Any]:
    """
    An internal helper function used by unit tests to compare configs.
    """
    return asdict(config)
