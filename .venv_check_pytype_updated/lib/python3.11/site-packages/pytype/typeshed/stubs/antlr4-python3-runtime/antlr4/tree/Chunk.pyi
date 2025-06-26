from _typeshed import Incomplete

class Chunk: ...

class TagChunk(Chunk):
    tag: Incomplete
    label: Incomplete
    def __init__(self, tag: str, label: str | None = None) -> None: ...

class TextChunk(Chunk):
    text: Incomplete
    def __init__(self, text: str) -> None: ...
