from _typeshed import Incomplete

from antlr4.atn.ATNState import RuleStartState
from antlr4.IntervalSet import IntervalSet

class Transition:
    EPSILON: int
    RANGE: int
    RULE: int
    PREDICATE: int
    ATOM: int
    ACTION: int
    SET: int
    NOT_SET: int
    WILDCARD: int
    PRECEDENCE: int
    serializationNames: Incomplete
    serializationTypes: Incomplete
    target: Incomplete
    isEpsilon: bool
    label: Incomplete
    def __init__(self, target) -> None: ...

class AtomTransition(Transition):
    label_: Incomplete
    label: Incomplete
    serializationType: Incomplete
    def __init__(self, target, label: int) -> None: ...
    def makeLabel(self): ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...

class RuleTransition(Transition):
    ruleIndex: Incomplete
    precedence: Incomplete
    followState: Incomplete
    serializationType: Incomplete
    isEpsilon: bool
    def __init__(self, ruleStart: RuleStartState, ruleIndex: int, precedence: int, followState) -> None: ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...

class EpsilonTransition(Transition):
    serializationType: Incomplete
    isEpsilon: bool
    outermostPrecedenceReturn: Incomplete
    def __init__(self, target, outermostPrecedenceReturn: int = -1) -> None: ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...

class RangeTransition(Transition):
    serializationType: Incomplete
    start: Incomplete
    stop: Incomplete
    label: Incomplete
    def __init__(self, target, start: int, stop: int) -> None: ...
    def makeLabel(self): ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...

class AbstractPredicateTransition(Transition):
    def __init__(self, target) -> None: ...

class PredicateTransition(AbstractPredicateTransition):
    serializationType: Incomplete
    ruleIndex: Incomplete
    predIndex: Incomplete
    isCtxDependent: Incomplete
    isEpsilon: bool
    def __init__(self, target, ruleIndex: int, predIndex: int, isCtxDependent: bool) -> None: ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...
    def getPredicate(self): ...

class ActionTransition(Transition):
    serializationType: Incomplete
    ruleIndex: Incomplete
    actionIndex: Incomplete
    isCtxDependent: Incomplete
    isEpsilon: bool
    def __init__(self, target, ruleIndex: int, actionIndex: int = -1, isCtxDependent: bool = False) -> None: ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...

class SetTransition(Transition):
    serializationType: Incomplete
    label: Incomplete
    def __init__(self, target, set: IntervalSet) -> None: ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...

class NotSetTransition(SetTransition):
    serializationType: Incomplete
    def __init__(self, target, set: IntervalSet) -> None: ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...

class WildcardTransition(Transition):
    serializationType: Incomplete
    def __init__(self, target) -> None: ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...

class PrecedencePredicateTransition(AbstractPredicateTransition):
    serializationType: Incomplete
    precedence: Incomplete
    isEpsilon: bool
    def __init__(self, target, precedence: int) -> None: ...
    def matches(self, symbol: int, minVocabSymbol: int, maxVocabSymbol: int): ...
    def getPredicate(self): ...
