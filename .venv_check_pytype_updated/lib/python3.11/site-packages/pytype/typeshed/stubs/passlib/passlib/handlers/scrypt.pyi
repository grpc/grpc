from _typeshed import Incomplete
from typing import ClassVar

import passlib.utils.handlers as uh

class scrypt(uh.ParallelismMixin, uh.HasRounds, uh.HasRawSalt, uh.HasRawChecksum, uh.HasManyIdents, uh.GenericHandler):  # type: ignore[misc]
    backends: ClassVar[tuple[str, ...]]
    name: ClassVar[str]
    checksum_size: ClassVar[int]
    default_ident: ClassVar[str]
    ident_values: ClassVar[tuple[str, ...]]
    default_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    parallelism: int
    block_size: int
    @classmethod
    def using(cls, block_size: Incomplete | None = None, **kwds): ...  # type: ignore[override]
    @classmethod
    def from_string(cls, hash): ...
    @classmethod
    def parse(cls, hash): ...
    def to_string(self): ...
    def __init__(self, block_size: Incomplete | None = None, **kwds) -> None: ...
    @classmethod
    def get_backend(cls): ...
    @classmethod
    def has_backend(cls, name: str = "any"): ...
    @classmethod
    def set_backend(cls, name: str = "any", dryrun: bool = False) -> None: ...
