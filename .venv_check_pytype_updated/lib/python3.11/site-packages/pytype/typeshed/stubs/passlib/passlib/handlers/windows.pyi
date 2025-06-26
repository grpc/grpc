from _typeshed import Incomplete
from typing import Any, ClassVar

import passlib.utils.handlers as uh

class lmhash(uh.TruncateMixin, uh.HasEncodingContext, uh.StaticHandler):
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]
    truncate_size: ClassVar[int]
    @classmethod
    def raw(cls, secret, encoding: Incomplete | None = None): ...

class nthash(uh.StaticHandler):
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]
    @classmethod
    def raw(cls, secret): ...
    @classmethod
    def raw_nthash(cls, secret, hex: bool = False): ...

bsd_nthash: Any

class msdcc(uh.HasUserContext, uh.StaticHandler):
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]
    @classmethod
    def raw(cls, secret, user): ...

class msdcc2(uh.HasUserContext, uh.StaticHandler):
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]
    @classmethod
    def raw(cls, secret, user): ...
