from typing import ClassVar

import passlib.utils.handlers as uh

class _MD5_Common(uh.HasSalt, uh.GenericHandler):
    checksum_size: ClassVar[int]
    checksum_chars: ClassVar[str]
    max_salt_size: ClassVar[int]
    salt_chars: ClassVar[str]
    @classmethod
    def from_string(cls, hash): ...

class md5_crypt(uh.HasManyBackends, _MD5_Common):
    name: ClassVar[str]
    ident: ClassVar[str]
    backends: ClassVar[tuple[str, ...]]

class apr_md5_crypt(_MD5_Common):
    name: ClassVar[str]
    ident: ClassVar[str]
