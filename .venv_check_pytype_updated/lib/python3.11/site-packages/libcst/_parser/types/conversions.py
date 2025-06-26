# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable, Sequence

from libcst._parser.types.config import ParserConfig
from libcst._parser.types.token import Token

# pyre-fixme[33]: Aliased annotation cannot contain `Any`.
NonterminalConversion = Callable[[ParserConfig, Sequence[Any]], Any]
# pyre-fixme[33]: Aliased annotation cannot contain `Any`.
TerminalConversion = Callable[[ParserConfig, Token], Any]
