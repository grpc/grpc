# Copyright 2004-2005 Elemental Security, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# Modifications:
# Copyright David Halter and Contributors
# Modifications are dual-licensed: MIT and PSF.
# 99% of the code is different from pgen2, now.
#
# A fork of `parso.pgen2.grammar_parser`.
# https://github.com/davidhalter/parso/blob/master/parso/pgen2/grammar_parser.py
#
# The following changes were made:
# - Type stubs were directly applied.
# pyre-unsafe

from typing import Generator, List, Optional, Tuple

from libcst._parser.parso.python.token import PythonTokenTypes
from libcst._parser.parso.python.tokenize import tokenize
from libcst._parser.parso.utils import parse_version_string


class NFAArc:
    def __init__(self, next_: "NFAState", nonterminal_or_string: Optional[str]) -> None:
        self.next: NFAState = next_
        self.nonterminal_or_string: Optional[str] = nonterminal_or_string

    def __repr__(self) -> str:
        return "<%s: %s>" % (self.__class__.__name__, self.nonterminal_or_string)


class NFAState:
    def __init__(self, from_rule: str) -> None:
        self.from_rule = from_rule
        self.arcs: List[NFAArc] = []

    def add_arc(
        self, next_: "NFAState", nonterminal_or_string: Optional[str] = None
    ) -> None:
        self.arcs.append(NFAArc(next_, nonterminal_or_string))

    def __repr__(self) -> str:
        return "<%s: from %s>" % (self.__class__.__name__, self.from_rule)


class GrammarParser:
    """
    The parser for Python grammar files.
    """

    def __init__(self, bnf_grammar: str) -> None:
        self._bnf_grammar: str = bnf_grammar
        self.generator = tokenize(bnf_grammar, version_info=parse_version_string("3.6"))
        self._gettoken()  # Initialize lookahead

    def parse(self) -> Generator[Tuple[NFAState, NFAState], None, None]:
        # grammar: (NEWLINE | rule)* ENDMARKER
        while self.type != PythonTokenTypes.ENDMARKER:
            while self.type == PythonTokenTypes.NEWLINE:
                self._gettoken()

            # rule: NAME ':' rhs NEWLINE
            # pyre-ignore Pyre is unhappy with the fact that we haven't put
            # _current_rule_name in the constructor.
            self._current_rule_name = self._expect(PythonTokenTypes.NAME)
            self._expect(PythonTokenTypes.OP, ":")

            a, z = self._parse_rhs()
            self._expect(PythonTokenTypes.NEWLINE)

            yield a, z

    def _parse_rhs(self):
        # rhs: items ('|' items)*
        a, z = self._parse_items()
        if self.value != "|":
            return a, z
        else:
            aa = NFAState(self._current_rule_name)
            zz = NFAState(self._current_rule_name)
            while True:
                # Add the possibility to go into the state of a and come back
                # to finish.
                aa.add_arc(a)
                z.add_arc(zz)
                if self.value != "|":
                    break

                self._gettoken()
                a, z = self._parse_items()
            return aa, zz

    def _parse_items(self):
        # items: item+
        a, b = self._parse_item()
        while self.type in (
            PythonTokenTypes.NAME,
            PythonTokenTypes.STRING,
        ) or self.value in ("(", "["):
            c, d = self._parse_item()
            # Need to end on the next item.
            b.add_arc(c)
            b = d
        return a, b

    def _parse_item(self):
        # item: '[' rhs ']' | atom ['+' | '*']
        if self.value == "[":
            self._gettoken()
            a, z = self._parse_rhs()
            self._expect(PythonTokenTypes.OP, "]")
            # Make it also possible that there is no token and change the
            # state.
            a.add_arc(z)
            return a, z
        else:
            a, z = self._parse_atom()
            value = self.value
            if value not in ("+", "*"):
                return a, z
            self._gettoken()
            # Make it clear that we can go back to the old state and repeat.
            z.add_arc(a)
            if value == "+":
                return a, z
            else:
                # The end state is the same as the beginning, nothing must
                # change.
                return a, a

    def _parse_atom(self):
        # atom: '(' rhs ')' | NAME | STRING
        if self.value == "(":
            self._gettoken()
            a, z = self._parse_rhs()
            self._expect(PythonTokenTypes.OP, ")")
            return a, z
        elif self.type in (PythonTokenTypes.NAME, PythonTokenTypes.STRING):
            a = NFAState(self._current_rule_name)
            z = NFAState(self._current_rule_name)
            # Make it clear that the state transition requires that value.
            a.add_arc(z, self.value)
            self._gettoken()
            return a, z
        else:
            self._raise_error(
                "expected (...) or NAME or STRING, got %s/%s", self.type, self.value
            )

    def _expect(self, type_, value=None):
        if self.type != type_:
            self._raise_error("expected %s, got %s [%s]", type_, self.type, self.value)
        if value is not None and self.value != value:
            self._raise_error("expected %s, got %s", value, self.value)
        value = self.value
        self._gettoken()
        return value

    def _gettoken(self) -> None:
        tup = next(self.generator)
        self.type, self.value, self.begin, prefix = tup

    def _raise_error(self, msg: str, *args: object) -> None:
        if args:
            try:
                msg = msg % args
            except Exception:
                msg = " ".join([msg] + list(map(str, args)))
        line = self._bnf_grammar.splitlines()[self.begin[0] - 1]
        raise SyntaxError(msg, ("<grammar>", self.begin[0], self.begin[1], line))
