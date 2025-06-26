from _typeshed import Incomplete
from typing import Any, ClassVar

import passlib.utils.handlers as uh

class _DummyCffiHasher:
    time_cost: int
    memory_cost: int
    parallelism: int
    salt_len: int
    hash_len: int

class _Argon2Common(  # type: ignore[misc]
    uh.SubclassBackendMixin, uh.ParallelismMixin, uh.HasRounds, uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler
):
    name: ClassVar[str]
    checksum_size: ClassVar[int]
    default_salt_size: ClassVar[int]
    min_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    max_parallelism: ClassVar[int]
    max_version: ClassVar[int]
    min_desired_version: ClassVar[int | None]
    min_memory_cost: ClassVar[int]
    max_threads: ClassVar[int]
    pure_use_threads: ClassVar[bool]
    def type_values(cls): ...
    type: str
    parallelism: int
    version: int
    memory_cost: int
    @property
    def type_d(self): ...
    data: Any
    @classmethod
    def using(  # type: ignore[override]
        cls,
        type: Incomplete | None = None,
        memory_cost: Incomplete | None = None,
        salt_len: Incomplete | None = None,
        time_cost: Incomplete | None = None,
        digest_size: Incomplete | None = None,
        checksum_size: Incomplete | None = None,
        hash_len: Incomplete | None = None,
        max_threads: Incomplete | None = None,
        **kwds,
    ): ...
    @classmethod
    def identify(cls, hash): ...
    @classmethod
    def from_string(cls, hash): ...
    def __init__(
        self,
        type: Incomplete | None = None,
        type_d: bool = False,
        version: Incomplete | None = None,
        memory_cost: Incomplete | None = None,
        data: Incomplete | None = None,
        **kwds,
    ) -> None: ...

class _NoBackend(_Argon2Common):
    @classmethod
    def hash(cls, secret): ...
    @classmethod
    def verify(cls, secret, hash): ...
    @classmethod
    def genhash(cls, secret, config): ...

class _CffiBackend(_Argon2Common):
    @classmethod
    def hash(cls, secret): ...
    @classmethod
    def verify(cls, secret, hash): ...
    @classmethod
    def genhash(cls, secret, config): ...

class _PureBackend(_Argon2Common): ...

class argon2(_NoBackend, _Argon2Common):  # type: ignore[misc]
    backends: ClassVar[tuple[str, ...]]
