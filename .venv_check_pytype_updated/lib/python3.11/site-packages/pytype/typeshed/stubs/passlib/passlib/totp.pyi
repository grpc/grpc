from _typeshed import Incomplete
from typing import Any

from passlib.exc import (
    InvalidTokenError as InvalidTokenError,
    MalformedTokenError as MalformedTokenError,
    TokenError as TokenError,
    UsedTokenError as UsedTokenError,
)
from passlib.utils import SequenceMixin

class AppWallet:
    salt_size: int
    encrypt_cost: int
    default_tag: Any
    def __init__(
        self,
        secrets: Incomplete | None = None,
        default_tag: Incomplete | None = None,
        encrypt_cost: Incomplete | None = None,
        secrets_path: Incomplete | None = None,
    ) -> None: ...
    @property
    def has_secrets(self): ...
    def get_secret(self, tag): ...
    def encrypt_key(self, key): ...
    def decrypt_key(self, enckey): ...

class TOTP:
    min_json_version: int
    json_version: int
    wallet: Any
    now: Any
    digits: int
    alg: str
    label: Any
    issuer: Any
    period: int
    changed: bool
    @classmethod
    def using(
        cls,
        digits: Incomplete | None = None,
        alg: Incomplete | None = None,
        period: Incomplete | None = None,
        issuer: Incomplete | None = None,
        wallet: Incomplete | None = None,
        now: Incomplete | None = None,
        **kwds,
    ): ...
    @classmethod
    def new(cls, **kwds): ...
    def __init__(
        self,
        key: Incomplete | None = None,
        format: str = "base32",
        new: bool = False,
        digits: Incomplete | None = None,
        alg: Incomplete | None = None,
        size: Incomplete | None = None,
        period: Incomplete | None = None,
        label: Incomplete | None = None,
        issuer: Incomplete | None = None,
        changed: bool = False,
        **kwds,
    ) -> None: ...
    @property
    def key(self): ...
    @key.setter
    def key(self, value) -> None: ...
    @property
    def encrypted_key(self): ...
    @encrypted_key.setter
    def encrypted_key(self, value) -> None: ...
    @property
    def hex_key(self): ...
    @property
    def base32_key(self): ...
    def pretty_key(self, format: str = "base32", sep: str = "-"): ...
    @classmethod
    def normalize_time(cls, time): ...
    def normalize_token(self_or_cls, token): ...
    def generate(self, time: Incomplete | None = None): ...
    @classmethod
    def verify(cls, token, source, **kwds): ...
    def match(
        self, token, time: Incomplete | None = None, window: int = 30, skew: int = 0, last_counter: Incomplete | None = None
    ): ...
    @classmethod
    def from_source(cls, source): ...
    @classmethod
    def from_uri(cls, uri): ...
    def to_uri(self, label: Incomplete | None = None, issuer: Incomplete | None = None): ...
    @classmethod
    def from_json(cls, source): ...
    def to_json(self, encrypt: Incomplete | None = None): ...
    @classmethod
    def from_dict(cls, source): ...
    def to_dict(self, encrypt: Incomplete | None = None): ...

class TotpToken(SequenceMixin):
    totp: Any
    token: Any
    counter: Any
    def __init__(self, totp, token, counter) -> None: ...
    @property
    def start_time(self): ...
    @property
    def expire_time(self): ...
    @property
    def remaining(self): ...
    @property
    def valid(self): ...

class TotpMatch(SequenceMixin):
    totp: Any
    counter: int
    time: int
    window: int
    def __init__(self, totp, counter, time, window: int = 30) -> None: ...
    @property
    def expected_counter(self): ...
    @property
    def skipped(self): ...
    @property
    def expire_time(self): ...
    @property
    def cache_seconds(self): ...
    @property
    def cache_time(self): ...
