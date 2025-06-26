from typing import ClassVar
from typing_extensions import Self

import passlib.utils.handlers as uh
from passlib.utils.handlers import PrefixWrapper

class Pbkdf2DigestHandler(uh.HasRounds, uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler):  # type: ignore[misc]
    checksum_chars: ClassVar[str]
    default_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    @classmethod
    def from_string(cls, hash: str | bytes) -> Self: ...  # type: ignore[override]

# dynamically created by create_pbkdf2_hash()
class pbkdf2_sha1(Pbkdf2DigestHandler):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    encoded_checksum_size: ClassVar[int]

# dynamically created by create_pbkdf2_hash()
class pbkdf2_sha256(Pbkdf2DigestHandler):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    encoded_checksum_size: ClassVar[int]

# dynamically created by create_pbkdf2_hash()
class pbkdf2_sha512(Pbkdf2DigestHandler):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    encoded_checksum_size: ClassVar[int]

ldap_pbkdf2_sha1: PrefixWrapper
ldap_pbkdf2_sha256: PrefixWrapper
ldap_pbkdf2_sha512: PrefixWrapper

class cta_pbkdf2_sha1(uh.HasRounds, uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    default_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    @classmethod
    def from_string(cls, hash): ...

class dlitz_pbkdf2_sha1(uh.HasRounds, uh.HasSalt, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    ident: ClassVar[str]
    default_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    salt_chars: ClassVar[str]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    @classmethod
    def from_string(cls, hash): ...

class atlassian_pbkdf2_sha1(uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    min_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    @classmethod
    def from_string(cls, hash): ...

class grub_pbkdf2_sha512(uh.HasRounds, uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler):  # type: ignore[misc]
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    default_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    default_rounds: ClassVar[int]
    min_rounds: ClassVar[int]
    max_rounds: ClassVar[int]
    rounds_cost: ClassVar[str]
    @classmethod
    def from_string(cls, hash): ...
