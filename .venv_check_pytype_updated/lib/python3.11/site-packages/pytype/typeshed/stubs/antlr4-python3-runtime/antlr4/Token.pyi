from _typeshed import Incomplete

class Token:
    INVALID_TYPE: int
    EPSILON: int
    MIN_USER_TOKEN_TYPE: int
    EOF: int
    DEFAULT_CHANNEL: int
    HIDDEN_CHANNEL: int
    source: Incomplete
    type: Incomplete
    channel: Incomplete
    start: Incomplete
    stop: Incomplete
    tokenIndex: Incomplete
    line: Incomplete
    column: Incomplete
    def __init__(self) -> None: ...
    @property
    def text(self): ...
    @text.setter
    def text(self, text: str): ...
    def getTokenSource(self): ...
    def getInputStream(self): ...

class CommonToken(Token):
    EMPTY_SOURCE: Incomplete
    source: Incomplete
    type: Incomplete
    channel: Incomplete
    start: Incomplete
    stop: Incomplete
    tokenIndex: int
    line: Incomplete
    column: Incomplete
    def __init__(
        self,
        source: tuple[Incomplete, Incomplete] = (None, None),
        type: int | None = None,
        channel: int = 0,
        start: int = -1,
        stop: int = -1,
    ) -> None: ...
    def clone(self): ...
    @property
    def text(self): ...
    @text.setter
    def text(self, text: str): ...
