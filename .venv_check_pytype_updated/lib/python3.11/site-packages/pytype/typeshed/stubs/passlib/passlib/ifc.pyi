from abc import ABCMeta, abstractmethod
from typing import Any, ClassVar, Literal
from typing_extensions import Self

class PasswordHash(metaclass=ABCMeta):
    is_disabled: ClassVar[bool]
    truncate_size: ClassVar[int | None]
    truncate_error: ClassVar[bool]
    truncate_verify_reject: ClassVar[bool]
    @classmethod
    @abstractmethod
    def hash(cls, secret: str | bytes, **setting_and_context_kwds) -> str: ...
    @classmethod
    def encrypt(cls, secret: str | bytes, **kwds) -> str: ...
    @classmethod
    @abstractmethod
    def verify(cls, secret: str | bytes, hash: str | bytes, **context_kwds): ...
    @classmethod
    @abstractmethod
    def using(cls, relaxed: bool = False, **kwds: Any) -> type[Self]: ...
    @classmethod
    def needs_update(cls, hash: str, secret: str | bytes | None = None) -> bool: ...
    @classmethod
    @abstractmethod
    def identify(cls, hash: str | bytes) -> bool: ...
    @classmethod
    def genconfig(cls, **setting_kwds: Any) -> str: ...
    @classmethod
    def genhash(cls, secret: str | bytes, config: str, **context: Any) -> str: ...
    deprecated: bool

class DisabledHash(PasswordHash, metaclass=ABCMeta):
    is_disabled: ClassVar[Literal[True]]
    @classmethod
    def disable(cls, hash: str | None = None) -> str: ...
    @classmethod
    def enable(cls, hash: str) -> str: ...
