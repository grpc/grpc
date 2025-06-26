from typing import ClassVar

import passlib.utils.handlers as uh

__all__: list[str] = []

class oracle10(uh.HasUserContext, uh.StaticHandler):
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]

class oracle11(uh.HasSalt, uh.GenericHandler):
    name: ClassVar[str]
    checksum_size: ClassVar[int]
    checksum_chars: ClassVar[str]
    min_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    salt_chars: ClassVar[str]
    @classmethod
    def from_string(cls, hash): ...
