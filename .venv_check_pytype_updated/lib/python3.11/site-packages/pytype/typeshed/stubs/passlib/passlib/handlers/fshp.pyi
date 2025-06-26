from typing import Any, ClassVar

import passlib.utils.handlers as uh

class fshp(uh.HasRounds, uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    ident: ClassVar[str]
    default_salt_size: ClassVar[int]
    max_salt_size: ClassVar[None]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    default_variant: ClassVar[int]
    @classmethod
    def using(cls, variant: int | str | bytes | None = None, **kwds): ...  # type: ignore[override]
    variant: int | None
    use_defaults: Any
    def __init__(self, variant: int | str | bytes | None = None, **kwds) -> None: ...
    @property
    def checksum_alg(self): ...
    @property
    def checksum_size(self): ...
    @classmethod
    def from_string(cls, hash): ...
