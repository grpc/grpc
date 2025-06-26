from typing import ClassVar

import passlib.utils.handlers as uh

__all__ = ["mysql323"]

class mysql323(uh.StaticHandler):
    name: ClassVar[str]
    checksum_size: ClassVar[int]
    checksum_chars: ClassVar[str]

class mysql41(uh.StaticHandler):
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]
