from typing import ClassVar

import passlib.utils.handlers as uh
from passlib.handlers.misc import plaintext
from passlib.utils.handlers import PrefixWrapper

__all__ = [
    "ldap_plaintext",
    "ldap_md5",
    "ldap_sha1",
    "ldap_salted_md5",
    "ldap_salted_sha1",
    "ldap_salted_sha256",
    "ldap_salted_sha512",
    "ldap_des_crypt",
    "ldap_bsdi_crypt",
    "ldap_md5_crypt",
    "ldap_sha1_crypt",
    "ldap_bcrypt",
    "ldap_sha256_crypt",
    "ldap_sha512_crypt",
]

class _Base64DigestHelper(uh.StaticHandler):
    ident: ClassVar[str | None]
    checksum_chars: ClassVar[str]

class _SaltedBase64DigestHelper(uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler):
    checksum_chars: ClassVar[str]
    ident: ClassVar[str | None]
    min_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    default_salt_size: ClassVar[int]
    @classmethod
    def from_string(cls, hash): ...

class ldap_md5(_Base64DigestHelper):
    name: ClassVar[str]
    ident: ClassVar[str]

class ldap_sha1(_Base64DigestHelper):
    name: ClassVar[str]
    ident: ClassVar[str]

class ldap_salted_md5(_SaltedBase64DigestHelper):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]

class ldap_salted_sha1(_SaltedBase64DigestHelper):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]

class ldap_salted_sha256(_SaltedBase64DigestHelper):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    default_salt_size: ClassVar[int]

class ldap_salted_sha512(_SaltedBase64DigestHelper):
    name: ClassVar[str]
    ident: ClassVar[str]
    checksum_size: ClassVar[int]
    default_salt_size: ClassVar[int]

class ldap_plaintext(plaintext):
    name: ClassVar[str]
    @classmethod
    def genconfig(cls): ...
    @classmethod
    def identify(cls, hash): ...

# Dynamically created
ldap_sha512_crypt: PrefixWrapper
ldap_sha256_crypt: PrefixWrapper
ldap_sha1_crypt: PrefixWrapper
ldap_bcrypt: PrefixWrapper
ldap_md5_crypt: PrefixWrapper
ldap_bsdi_crypt: PrefixWrapper
ldap_des_crypt: PrefixWrapper
