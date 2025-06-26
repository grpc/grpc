from typing import Any, ClassVar

import passlib.utils.handlers as uh
from passlib.handlers.bcrypt import _wrapped_bcrypt
from passlib.ifc import DisabledHash

class DjangoSaltedHash(uh.HasSalt, uh.GenericHandler):
    default_salt_size: ClassVar[int]
    salt_chars: ClassVar[str]
    checksum_chars: ClassVar[str]
    @classmethod
    def from_string(cls, hash): ...

class DjangoVariableHash(uh.HasRounds, DjangoSaltedHash):  # type: ignore[misc]
    min_rounds: ClassVar[int]
    @classmethod
    def from_string(cls, hash): ...

class django_salted_sha1(DjangoSaltedHash):
    name: ClassVar[str]
    django_name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]

class django_salted_md5(DjangoSaltedHash):
    name: ClassVar[str]
    django_name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]

django_bcrypt: Any

class django_bcrypt_sha256(_wrapped_bcrypt):
    name: ClassVar[str]
    django_name: ClassVar[str]
    django_prefix: ClassVar[str]
    @classmethod
    def identify(cls, hash): ...
    @classmethod
    def from_string(cls, hash): ...

class django_pbkdf2_sha256(DjangoVariableHash):
    name: ClassVar[str]
    django_name: ClassVar[str]
    ident: ClassVar[str]
    min_salt_size: ClassVar[int]
    max_rounds: ClassVar[int]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]
    default_rounds: ClassVar[int]

class django_pbkdf2_sha1(django_pbkdf2_sha256):
    name: ClassVar[str]
    django_name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    default_rounds: ClassVar[int]

django_argon2: Any

class django_des_crypt(uh.TruncateMixin, uh.HasSalt, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    django_name: ClassVar[str]
    ident: ClassVar[str]
    checksum_chars: ClassVar[str]
    salt_chars: ClassVar[str]
    checksum_size: ClassVar[int]
    min_salt_size: ClassVar[int]
    default_salt_size: ClassVar[int]
    truncate_size: ClassVar[int]
    use_duplicate_salt: bool
    @classmethod
    def from_string(cls, hash): ...

class django_disabled(DisabledHash, uh.StaticHandler):
    name: ClassVar[str]
    suffix_length: ClassVar[int]
    @classmethod
    def identify(cls, hash: str | bytes) -> bool: ...
    @classmethod
    def verify(cls, secret: str | bytes, hash: str | bytes) -> bool: ...  # type: ignore[override]
