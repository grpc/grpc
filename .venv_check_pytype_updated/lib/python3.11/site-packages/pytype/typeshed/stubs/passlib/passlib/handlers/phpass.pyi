from typing import ClassVar
from typing_extensions import Self

import passlib.utils.handlers as uh

class phpass(uh.HasManyIdents, uh.HasRounds, uh.HasSalt, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    min_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    salt_chars: ClassVar[str]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    default_ident: ClassVar[str]
    ident_values: ClassVar[tuple[str, ...]]
    ident_aliases: ClassVar[dict[str, str]]
    @classmethod
    def from_string(cls, hash: str | bytes) -> Self: ...  # type: ignore[override]
