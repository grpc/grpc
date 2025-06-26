from _typeshed import Incomplete

from antlr4.atn.ATN import ATN as ATN
from antlr4.atn.ATNState import ATNState as ATNState, LoopEndState as LoopEndState, StarLoopEntryState as StarLoopEntryState
from antlr4.atn.ParserATNSimulator import ParserATNSimulator as ParserATNSimulator
from antlr4.atn.Transition import Transition as Transition
from antlr4.BufferedTokenStream import TokenStream as TokenStream
from antlr4.dfa.DFA import DFA as DFA
from antlr4.error.Errors import (
    FailedPredicateException as FailedPredicateException,
    RecognitionException as RecognitionException,
    UnsupportedOperationException as UnsupportedOperationException,
)
from antlr4.Lexer import Lexer as Lexer
from antlr4.Parser import Parser as Parser
from antlr4.ParserRuleContext import InterpreterRuleContext as InterpreterRuleContext, ParserRuleContext as ParserRuleContext
from antlr4.PredictionContext import PredictionContextCache as PredictionContextCache
from antlr4.Token import Token as Token

class ParserInterpreter(Parser):
    grammarFileName: Incomplete
    atn: Incomplete
    tokenNames: Incomplete
    ruleNames: Incomplete
    decisionToDFA: Incomplete
    sharedContextCache: Incomplete
    pushRecursionContextStates: Incomplete
    def __init__(
        self, grammarFileName: str, tokenNames: list[str], ruleNames: list[str], atn: ATN, input: TokenStream
    ) -> None: ...
    state: Incomplete
    def parse(self, startRuleIndex: int): ...
    def enterRecursionRule(self, localctx: ParserRuleContext, state: int, ruleIndex: int, precedence: int): ...
    def getATNState(self): ...
    def visitState(self, p: ATNState): ...
    def visitRuleStopState(self, p: ATNState): ...
