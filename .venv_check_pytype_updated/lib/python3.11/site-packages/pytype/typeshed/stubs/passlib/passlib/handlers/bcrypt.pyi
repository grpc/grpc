from _typeshed import Incomplete
from typing import Any, ClassVar

import passlib.utils.handlers as uh

class _BcryptCommon(uh.SubclassBackendMixin, uh.TruncateMixin, uh.HasManyIdents, uh.HasRounds, uh.HasSalt, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    checksum_size: ClassVar[int]
    checksum_chars: ClassVar[str]
    default_ident: ClassVar[str]
    ident_values: ClassVar[tuple[str, ...]]
    ident_aliases: ClassVar[dict[str, str]]
    min_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    salt_chars: ClassVar[str]
    final_salt_chars: ClassVar[str]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    truncate_size: ClassVar[int | None]
    @classmethod
    def from_string(cls, hash): ...
    @classmethod
    def needs_update(cls, hash, **kwds): ...
    @classmethod
    def normhash(cls, hash): ...

class _NoBackend(_BcryptCommon): ...
class _BcryptBackend(_BcryptCommon): ...
class _BcryptorBackend(_BcryptCommon): ...
class _PyBcryptBackend(_BcryptCommon): ...
class _OsCryptBackend(_BcryptCommon): ...
class _BuiltinBackend(_BcryptCommon): ...

class bcrypt(_NoBackend, _BcryptCommon):  # type: ignore[misc]
    backends: ClassVar[tuple[str, ...]]

class _wrapped_bcrypt(bcrypt):
    truncate_size: ClassVar[None]

class bcrypt_sha256(_wrapped_bcrypt):
    name: ClassVar[str]
    ident_values: ClassVar[tuple[str, ...]]
    ident_aliases: ClassVar[dict[str, str]]
    default_ident: ClassVar[str]
    version: ClassVar[int]
    @classmethod
    def using(cls, version: Incomplete | None = None, **kwds): ...  # type: ignore[override]
    prefix: Any
    @classmethod
    def identify(cls, hash): ...
    @classmethod
    def from_string(cls, hash): ...
    def __init__(self, version: Incomplete | None = None, **kwds) -> None: ...
