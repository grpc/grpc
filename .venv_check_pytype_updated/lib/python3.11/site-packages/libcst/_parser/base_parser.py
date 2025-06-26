# Copyright 2004-2005 Elemental Security, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.

# Modifications:
# Copyright David Halter and Contributors
# Modifications are dual-licensed: MIT and PSF.
# 99% of the code is different from pgen2, now.

# A fork of `parso.parser`.
# https://github.com/davidhalter/parso/blob/v0.3.4/parso/parser.py
#
# The following changes were made:
# - Typing was added.
# - Error recovery is removed.
# - The Jedi-specific _allowed_transition_names_and_token_types API is removed.
# - Improved error messages by using our exceptions module.
# - node_map/leaf_map were removed in favor of just calling convert_*.
# - convert_node/convert_leaf were renamed to convert_nonterminal/convert_terminal
# - convert_nonterminal is called regardless of the number of children. Parso avoids
#   calling it in some cases to avoid creating extra nodes.
# - The parser is constructed with the tokens to allow us to track a bit more state. As
#   As a consequence parser may only be used once.
# - Supports our custom Token class, instead of `parso.python.tokenize.Token`.


from dataclasses import dataclass, field
from typing import Generic, Iterable, List, Sequence, TypeVar, Union

from libcst._exceptions import EOFSentinel, ParserSyntaxError, PartialParserSyntaxError
from libcst._parser._parsing_check import get_expected_str
from libcst._parser.parso.pgen2.generator import DFAState, Grammar, ReservedString
from libcst._parser.parso.python.token import TokenType
from libcst._parser.types.token import Token

_NodeT = TypeVar("_NodeT")
_TokenTypeT = TypeVar("_TokenTypeT", bound=TokenType)
_TokenT = TypeVar("_TokenT", bound=Token)


@dataclass(frozen=False)
class StackNode(Generic[_TokenTypeT, _NodeT]):
    dfa: "DFAState[_TokenTypeT]"
    nodes: List[_NodeT] = field(default_factory=list)

    @property
    def nonterminal(self) -> str:
        return self.dfa.from_rule


def _token_to_transition(
    grammar: "Grammar[_TokenTypeT]", type_: _TokenTypeT, value: str
) -> Union[ReservedString, _TokenTypeT]:
    # Map from token to label
    if type_.contains_syntax:
        # Check for reserved words (keywords)
        try:
            return grammar.reserved_syntax_strings[value]
        except KeyError:
            pass

    return type_


# TODO: This should be an ABC, but there's a metaclass conflict between Generic and ABC
# that's fixed in Python 3.7.
class BaseParser(Generic[_TokenT, _TokenTypeT, _NodeT]):
    """Parser engine.

    A Parser instance contains state pertaining to the current token
    sequence, and should not be used concurrently by different threads
    to parse separate token sequences.

    See python/tokenize.py for how to get input tokens by a string.
    """

    tokens: Iterable[_TokenT]
    lines: Sequence[str]  # used when generating parse errors
    _pgen_grammar: "Grammar[_TokenTypeT]"
    stack: List[StackNode[_TokenTypeT, _NodeT]]
    # Keep track of if parse was called. Because a parser may keep global mutable state,
    # each BaseParser instance should only be used once.
    __was_parse_called: bool

    def __init__(
        self,
        *,
        tokens: Iterable[_TokenT],
        lines: Sequence[str],
        pgen_grammar: "Grammar[_TokenTypeT]",
        start_nonterminal: str,
    ) -> None:
        self.tokens = tokens
        self.lines = lines
        self._pgen_grammar = pgen_grammar
        first_dfa = pgen_grammar.nonterminal_to_dfas[start_nonterminal][0]
        self.stack = [StackNode(first_dfa)]
        self.__was_parse_called = False

    def parse(self) -> _NodeT:
        # Ensure that we don't re-use parsers.
        if self.__was_parse_called:
            raise ValueError("Each parser object may only be used to parse once.")
        self.__was_parse_called = True

        for token in self.tokens:
            self._add_token(token)

        while True:
            tos = self.stack[-1]
            if not tos.dfa.is_final:
                expected_str = get_expected_str(
                    EOFSentinel.EOF, tos.dfa.transitions.keys()
                )
                raise ParserSyntaxError(
                    f"Incomplete input. {expected_str}",
                    lines=self.lines,
                    raw_line=len(self.lines),
                    raw_column=len(self.lines[-1]),
                )

            if len(self.stack) > 1:
                self._pop()
            else:
                return self.convert_nonterminal(tos.nonterminal, tos.nodes)

    def convert_nonterminal(
        self, nonterminal: str, children: Sequence[_NodeT]
    ) -> _NodeT: ...

    def convert_terminal(self, token: _TokenT) -> _NodeT: ...

    def _add_token(self, token: _TokenT) -> None:
        """
        This is the only core function for parsing. Here happens basically
        everything. Everything is well prepared by the parser generator and we
        only apply the necessary steps here.
        """
        grammar = self._pgen_grammar
        stack = self.stack
        # pyre-fixme[6]: Expected `_TokenTypeT` for 2nd param but got `TokenType`.
        transition = _token_to_transition(grammar, token.type, token.string)

        while True:
            try:
                plan = stack[-1].dfa.transitions[transition]
                break
            except KeyError:
                if stack[-1].dfa.is_final:
                    try:
                        self._pop()
                    except PartialParserSyntaxError as ex:
                        # Upconvert the PartialParserSyntaxError to a ParserSyntaxError
                        # by backfilling the line/column information.
                        raise ParserSyntaxError(
                            ex.message,
                            lines=self.lines,
                            raw_line=token.start_pos[0],
                            raw_column=token.start_pos[1],
                        )
                    except Exception as ex:
                        # convert_nonterminal may fail due to a bug in our code. Try to
                        # recover enough to at least tell us where in the file it
                        # failed.
                        raise ParserSyntaxError(
                            f"Internal error: {ex}",
                            lines=self.lines,
                            raw_line=token.start_pos[0],
                            raw_column=token.start_pos[1],
                        )
                else:
                    # We never broke out -- EOF is too soon -- Unfinished statement.
                    #
                    # BUG: The `expected_str` may not be complete because we already
                    # popped the other possibilities off the stack at this point, but
                    # it still seems useful to list some of the possibilities that we
                    # could've expected.
                    expected_str = get_expected_str(
                        token, stack[-1].dfa.transitions.keys()
                    )
                    raise ParserSyntaxError(
                        f"Incomplete input. {expected_str}",
                        lines=self.lines,
                        raw_line=token.start_pos[0],
                        raw_column=token.start_pos[1],
                    )
            except IndexError:
                # I don't think this will ever happen with Python's grammar, because if
                # there are any extra tokens at the end of the input, we'll instead
                # complain that we expected ENDMARKER.
                #
                # However, let's leave it just in case.
                expected_str = get_expected_str(token, EOFSentinel.EOF)
                raise ParserSyntaxError(
                    f"Too much input. {expected_str}",
                    lines=self.lines,
                    raw_line=token.start_pos[0],
                    raw_column=token.start_pos[1],
                )

        # Logically, `plan` is always defined, but pyre can't reasonably determine that.
        stack[-1].dfa = plan.next_dfa

        for push in plan.dfa_pushes:
            stack.append(StackNode(push))

        leaf = self.convert_terminal(token)
        stack[-1].nodes.append(leaf)

    def _pop(self) -> None:
        tos = self.stack.pop()
        # Unlike parso and lib2to3, we call `convert_nonterminal` unconditionally
        # instead of only when we have more than one child. This allows us to create a
        # far more consistent and predictable tree.
        new_node = self.convert_nonterminal(tos.dfa.from_rule, tos.nodes)
        self.stack[-1].nodes.append(new_node)
