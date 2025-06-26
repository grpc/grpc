from typing import ClassVar

import passlib.utils.handlers as uh

class mssql2000(uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler):
    name: ClassVar[str]
    checksum_size: ClassVar[int]
    min_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    @classmethod
    def from_string(cls, hash): ...
    @classmethod
    def verify(cls, secret: str | bytes, hash: str | bytes) -> bool: ...  # type: ignore[override]

class mssql2005(uh.HasRawSalt, uh.HasRawChecksum, uh.GenericHandler):
    name: ClassVar[str]
    checksum_size: ClassVar[int]
    min_salt_size: ClassVar[int]
    max_salt_size: ClassVar[int]
    @classmethod
    def from_string(cls, hash): ...
