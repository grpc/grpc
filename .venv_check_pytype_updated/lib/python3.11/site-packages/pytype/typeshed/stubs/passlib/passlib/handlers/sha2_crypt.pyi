from typing import ClassVar
from typing_extensions import Self

import passlib.utils.handlers as uh

class _SHA2_Common(uh.HasManyBackends, uh.HasRounds, uh.HasSalt, uh.GenericHandler):  # type: ignore[misc]
    checksum_chars: ClassVar[str]
    max_salt_size: ClassVar[int]
    salt_chars: ClassVar[str]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    implicit_rounds: bool
    def __init__(self, implicit_rounds: bool | None = None, **kwds) -> None: ...
    @classmethod
    def from_string(cls, hash: str | bytes) -> Self: ...  # type: ignore[override]
    backends: ClassVar[tuple[str, ...]]

class sha256_crypt(_SHA2_Common):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    default_rounds: ClassVar[int]

class sha512_crypt(_SHA2_Common):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    default_rounds: ClassVar[int]
