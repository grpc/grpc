from collections.abc import Generator
from typing import Any

class ScryptEngine:
    n: int
    r: int
    p: int
    smix_bytes: int
    iv_bytes: int
    bmix_len: int
    bmix_half_len: int
    bmix_struct: Any
    integerify: Any
    @classmethod
    def execute(cls, secret, salt, n, r, p, keylen): ...
    def __init__(self, n, r, p): ...
    def run(self, secret, salt, keylen): ...
    def smix(self, input) -> Generator[None, None, Any]: ...
    def bmix(self, source, target) -> None: ...
