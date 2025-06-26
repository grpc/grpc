from typing import Any

class Behavior:
    name: str
    description: str
    versionString: str
    knownChildren: Any
    quotedPrintable: bool
    defaultBehavior: Any
    hasNative: bool
    isComponent: bool
    allowGroup: bool
    forceUTC: bool
    sortFirst: Any
    @classmethod
    def validate(cls, obj, raiseException: bool = False, complainUnrecognized: bool = False): ...
    @classmethod
    def lineValidate(cls, line, raiseException, complainUnrecognized): ...
    @classmethod
    def decode(cls, line) -> None: ...
    @classmethod
    def encode(cls, line) -> None: ...
    @classmethod
    def transformToNative(cls, obj): ...
    @classmethod
    def transformFromNative(cls, obj) -> None: ...
    @classmethod
    def generateImplicitParameters(cls, obj) -> None: ...
    @classmethod
    def serialize(cls, obj, buf, lineLength, validate: bool = True): ...
    @classmethod
    def valueRepr(cls, line): ...
