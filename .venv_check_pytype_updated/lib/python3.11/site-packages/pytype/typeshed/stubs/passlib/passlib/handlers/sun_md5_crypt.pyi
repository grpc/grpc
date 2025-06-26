from typing import ClassVar
from typing_extensions import Self

import passlib.utils.handlers as uh

class sun_md5_crypt(uh.HasRounds, uh.HasSalt, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]
    default_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int | None]
    salt_chars: ClassVar[str]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    ident_values: ClassVar[tuple[str, ...]]
    bare_salt: bool
    def __init__(self, bare_salt: bool = False, **kwds) -> None: ...
    @classmethod
    def identify(cls, hash): ...
    @classmethod
    def from_string(cls, hash: str | bytes) -> Self: ...  # type: ignore[override]
    def to_string(self, _withchk: bool = True) -> str: ...
