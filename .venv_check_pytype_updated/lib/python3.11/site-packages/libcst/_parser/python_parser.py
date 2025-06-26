# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
# pyre-unsafe

from typing import Any, Iterable, Mapping, Sequence

from libcst._parser.base_parser import BaseParser
from libcst._parser.grammar import get_nonterminal_conversions, get_terminal_conversions
from libcst._parser.parso.pgen2.generator import Grammar
from libcst._parser.parso.python.token import TokenType
from libcst._parser.types.config import ParserConfig
from libcst._parser.types.conversions import NonterminalConversion, TerminalConversion
from libcst._parser.types.token import Token


class PythonCSTParser(BaseParser[Token, TokenType, Any]):
    config: ParserConfig
    terminal_conversions: Mapping[str, TerminalConversion]
    nonterminal_conversions: Mapping[str, NonterminalConversion]

    def __init__(
        self,
        *,
        tokens: Iterable[Token],
        config: ParserConfig,
        pgen_grammar: "Grammar[TokenType]",
        start_nonterminal: str = "file_input",
    ) -> None:
        super().__init__(
            tokens=tokens,
            lines=config.lines,
            pgen_grammar=pgen_grammar,
            start_nonterminal=start_nonterminal,
        )
        self.config = config
        self.terminal_conversions = get_terminal_conversions()
        self.nonterminal_conversions = get_nonterminal_conversions(
            config.version, config.future_imports
        )

    def convert_nonterminal(self, nonterminal: str, children: Sequence[Any]) -> Any:
        return self.nonterminal_conversions[nonterminal](self.config, children)

    def convert_terminal(self, token: Token) -> Any:
        return self.terminal_conversions[token.type.name](self.config, token)
