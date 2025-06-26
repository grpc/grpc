from typing import Any, ClassVar
from typing_extensions import Self

import passlib.utils.handlers as uh

log: Any

class sha1_crypt(uh.HasManyBackends, uh.HasRounds, uh.HasSalt, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    checksum_chars: ClassVar[str]
    default_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    salt_chars: ClassVar[str]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    @classmethod
    def from_string(cls, hash: str | bytes) -> Self: ...  # type: ignore[override]
    def to_string(self, config: bool = False) -> str: ...
    backends: ClassVar[tuple[str, ...]]
