from typing import ClassVar

import passlib.utils.handlers as uh

class cisco_pix(uh.HasUserContext, uh.StaticHandler):
    name: ClassVar[str]
    truncate_size: ClassVar[int]
    truncate_error: ClassVar[bool]
    truncate_verify_reject: ClassVar[bool]
    checksum_size: ClassVar[int]
    checksum_chars: ClassVar[str]

class cisco_asa(cisco_pix): ...

class cisco_type7(uh.GenericHandler):
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    min_salt_value: ClassVar[int]
    max_salt_value: ClassVar[int]
    @classmethod
    def using(cls, salt: int | None = None, **kwds): ...  # type: ignore[override]
    @classmethod
    def from_string(cls, hash): ...
    salt: int
    def __init__(self, salt: int | None = None, **kwds) -> None: ...
    @classmethod
    def decode(cls, hash, encoding: str = "utf-8"): ...
