from enum import IntEnum

class ATNType(IntEnum):
    LEXER: int
    PARSER: int
    @classmethod
    def fromOrdinal(cls, i: int): ...
