import threading
from _typeshed import Incomplete, SupportsItems, Unused
from collections.abc import Callable, Iterable, Iterator, Mapping, Sequence
from datetime import datetime, timedelta
from re import Pattern
from types import TracebackType
from typing import Any, ClassVar, Literal, TypeVar, overload
from typing_extensions import Self, TypeAlias

from redis import RedisError

from .commands import CoreCommands, RedisModuleCommands, SentinelCommands
from .connection import ConnectionPool, _ConnectFunc, _ConnectionPoolOptions
from .credentials import CredentialProvider
from .lock import Lock
from .retry import Retry
from .typing import ChannelT, EncodableT, KeyT, PatternT

_Value: TypeAlias = bytes | float | int | str
_Key: TypeAlias = str | bytes

# Lib returns str or bytes depending on value of decode_responses
_StrType = TypeVar("_StrType", bound=str | bytes)

_VT = TypeVar("_VT")
_T = TypeVar("_T")

# Keyword arguments that are passed to Redis.parse_response().
_ParseResponseOptions: TypeAlias = Any
# Keyword arguments that are passed to Redis.execute_command().
_CommandOptions: TypeAlias = _ConnectionPoolOptions | _ParseResponseOptions

SYM_EMPTY: bytes
EMPTY_RESPONSE: str
NEVER_DECODE: str

class CaseInsensitiveDict(dict[_StrType, _VT]):
    def __init__(self, data: SupportsItems[_StrType, _VT]) -> None: ...
    def update(self, data: SupportsItems[_StrType, _VT]) -> None: ...  # type: ignore[override]
    @overload
    def get(self, k: _StrType, default: None = None) -> _VT | None: ...
    @overload
    def get(self, k: _StrType, default: _VT | _T) -> _VT | _T: ...
    # Overrides many other methods too, but without changing signature

def list_or_args(keys, args): ...
def timestamp_to_datetime(response): ...
def string_keys_to_dict(key_string, callback): ...
def parse_debug_object(response): ...
def parse_object(response, infotype): ...
def parse_info(response): ...

SENTINEL_STATE_TYPES: dict[str, type[int]]

def parse_sentinel_state(item): ...
def parse_sentinel_master(response): ...
def parse_sentinel_masters(response): ...
def parse_sentinel_slaves_and_sentinels(response): ...
def parse_sentinel_get_master(response): ...
def pairs_to_dict(response, decode_keys: bool = False, decode_string_values: bool = False): ...
def pairs_to_dict_typed(response, type_info): ...
def zset_score_pairs(response, **options): ...
def sort_return_tuples(response, **options): ...
def int_or_none(response): ...
def float_or_none(response): ...
def bool_ok(response): ...
def parse_client_list(response, **options): ...
def parse_config_get(response, **options): ...
def parse_scan(response, **options): ...
def parse_hscan(response, **options): ...
def parse_zscan(response, **options): ...
def parse_slowlog_get(response, **options): ...

_LockType = TypeVar("_LockType")

class AbstractRedis:
    RESPONSE_CALLBACKS: dict[str, Any]

class Redis(AbstractRedis, RedisModuleCommands, CoreCommands[_StrType], SentinelCommands):
    @overload
    @classmethod
    def from_url(
        cls,
        url: str,
        *,
        host: str | None = ...,
        port: int | None = ...,
        db: int | None = ...,
        password: str | None = ...,
        socket_timeout: float | None = ...,
        socket_connect_timeout: float | None = ...,
        socket_keepalive: bool | None = ...,
        socket_keepalive_options: Mapping[str, int | str] | None = ...,
        connection_pool: ConnectionPool | None = ...,
        unix_socket_path: str | None = ...,
        encoding: str = ...,
        encoding_errors: str = ...,
        charset: str | None = ...,
        errors: str | None = ...,
        decode_responses: Literal[True],
        retry_on_timeout: bool = ...,
        retry_on_error: list[type[RedisError]] | None = ...,
        ssl: bool = ...,
        ssl_keyfile: str | None = ...,
        ssl_certfile: str | None = ...,
        ssl_cert_reqs: str | int | None = ...,
        ssl_ca_certs: str | None = ...,
        ssl_check_hostname: bool = ...,
        max_connections: int | None = ...,
        single_connection_client: bool = ...,
        health_check_interval: float = ...,
        client_name: str | None = ...,
        username: str | None = ...,
        retry: Retry | None = ...,
    ) -> Redis[str]: ...
    @overload
    @classmethod
    def from_url(
        cls,
        url: str,
        *,
        host: str | None = ...,
        port: int | None = ...,
        db: int | None = ...,
        password: str | None = ...,
        socket_timeout: float | None = ...,
        socket_connect_timeout: float | None = ...,
        socket_keepalive: bool | None = ...,
        socket_keepalive_options: Mapping[str, int | str] | None = ...,
        connection_pool: ConnectionPool | None = ...,
        unix_socket_path: str | None = ...,
        encoding: str = ...,
        encoding_errors: str = ...,
        charset: str | None = ...,
        errors: str | None = ...,
        decode_responses: Literal[False] = False,
        retry_on_timeout: bool = ...,
        retry_on_error: list[type[RedisError]] | None = ...,
        ssl: bool = ...,
        ssl_keyfile: str | None = ...,
        ssl_certfile: str | None = ...,
        ssl_cert_reqs: str | int | None = ...,
        ssl_ca_certs: str | None = ...,
        ssl_check_hostname: bool = ...,
        max_connections: int | None = ...,
        single_connection_client: bool = ...,
        health_check_interval: float = ...,
        client_name: str | None = ...,
        username: str | None = ...,
        retry: Retry | None = ...,
    ) -> Redis[bytes]: ...
    connection_pool: Any
    response_callbacks: Any
    @overload
    def __init__(
        self: Redis[str],
        host: str,
        port: int,
        db: int,
        password: str | None,
        socket_timeout: float | None,
        socket_connect_timeout: float | None,
        socket_keepalive: bool | None,
        socket_keepalive_options: Mapping[str, int | str] | None,
        connection_pool: ConnectionPool | None,
        unix_socket_path: str | None,
        encoding: str,
        encoding_errors: str,
        charset: str | None,
        errors: str | None,
        decode_responses: Literal[True],
        retry_on_timeout: bool = False,
        retry_on_error: list[type[RedisError]] | None = None,
        ssl: bool = False,
        ssl_keyfile: str | None = None,
        ssl_certfile: str | None = None,
        ssl_cert_reqs: str | int | None = "required",
        ssl_ca_certs: str | None = None,
        ssl_ca_path: Incomplete | None = None,
        ssl_ca_data: Incomplete | None = None,
        ssl_check_hostname: bool = False,
        ssl_password: Incomplete | None = None,
        ssl_validate_ocsp: bool = False,
        ssl_validate_ocsp_stapled: bool = False,  # added in 4.1.1
        ssl_ocsp_context: Incomplete | None = None,  # added in 4.1.1
        ssl_ocsp_expected_cert: Incomplete | None = None,  # added in 4.1.1
        max_connections: int | None = None,
        single_connection_client: bool = False,
        health_check_interval: float = 0,
        client_name: str | None = None,
        username: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: _ConnectFunc | None = None,
        credential_provider: CredentialProvider | None = None,
    ) -> None: ...
    @overload
    def __init__(
        self: Redis[str],
        host: str = "localhost",
        port: int = 6379,
        db: int = 0,
        password: str | None = None,
        socket_timeout: float | None = None,
        socket_connect_timeout: float | None = None,
        socket_keepalive: bool | None = None,
        socket_keepalive_options: Mapping[str, int | str] | None = None,
        connection_pool: ConnectionPool | None = None,
        unix_socket_path: str | None = None,
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        charset: str | None = None,
        errors: str | None = None,
        *,
        decode_responses: Literal[True],
        retry_on_timeout: bool = False,
        retry_on_error: list[type[RedisError]] | None = None,
        ssl: bool = False,
        ssl_keyfile: str | None = None,
        ssl_certfile: str | None = None,
        ssl_cert_reqs: str | int | None = "required",
        ssl_ca_certs: str | None = None,
        ssl_ca_data: Incomplete | None = None,
        ssl_check_hostname: bool = False,
        ssl_password: Incomplete | None = None,
        ssl_validate_ocsp: bool = False,
        ssl_validate_ocsp_stapled: bool = False,  # added in 4.1.1
        ssl_ocsp_context: Incomplete | None = None,  # added in 4.1.1
        ssl_ocsp_expected_cert: Incomplete | None = None,  # added in 4.1.1
        max_connections: int | None = None,
        single_connection_client: bool = False,
        health_check_interval: float = 0,
        client_name: str | None = None,
        username: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: _ConnectFunc | None = None,
        credential_provider: CredentialProvider | None = None,
    ) -> None: ...
    @overload
    def __init__(
        self: Redis[bytes],
        host: str = "localhost",
        port: int = 6379,
        db: int = 0,
        password: str | None = None,
        socket_timeout: float | None = None,
        socket_connect_timeout: float | None = None,
        socket_keepalive: bool | None = None,
        socket_keepalive_options: Mapping[str, int | str] | None = None,
        connection_pool: ConnectionPool | None = None,
        unix_socket_path: str | None = None,
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        charset: str | None = None,
        errors: str | None = None,
        decode_responses: Literal[False] = False,
        retry_on_timeout: bool = False,
        retry_on_error: list[type[RedisError]] | None = None,
        ssl: bool = False,
        ssl_keyfile: str | None = None,
        ssl_certfile: str | None = None,
        ssl_cert_reqs: str | int | None = "required",
        ssl_ca_certs: str | None = None,
        ssl_ca_data: Incomplete | None = None,
        ssl_check_hostname: bool = False,
        ssl_password: Incomplete | None = None,
        ssl_validate_ocsp: bool = False,
        ssl_validate_ocsp_stapled: bool = False,  # added in 4.1.1
        ssl_ocsp_context: Incomplete | None = None,  # added in 4.1.1
        ssl_ocsp_expected_cert: Incomplete | None = None,  # added in 4.1.1
        max_connections: int | None = None,
        single_connection_client: bool = False,
        health_check_interval: float = 0,
        client_name: str | None = None,
        username: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: _ConnectFunc | None = None,
        credential_provider: CredentialProvider | None = None,
    ) -> None: ...
    def get_encoder(self): ...
    def get_connection_kwargs(self): ...
    def set_response_callback(self, command, callback): ...
    def pipeline(self, transaction: bool = True, shard_hint: Any = None) -> Pipeline[_StrType]: ...
    def transaction(self, func, *watches, **kwargs): ...
    @overload
    def lock(
        self,
        name: _Key,
        timeout: float | None = None,
        sleep: float = 0.1,
        blocking: bool = True,
        blocking_timeout: float | None = None,
        lock_class: None = None,
        thread_local: bool = True,
    ) -> Lock: ...
    @overload
    def lock(
        self,
        name: _Key,
        timeout: float | None,
        sleep: float,
        blocking: bool,
        blocking_timeout: float | None,
        lock_class: type[_LockType],
        thread_local: bool = True,
    ) -> _LockType: ...
    @overload
    def lock(
        self,
        name: _Key,
        timeout: float | None = None,
        sleep: float = 0.1,
        blocking: bool = True,
        blocking_timeout: float | None = None,
        *,
        lock_class: type[_LockType],
        thread_local: bool = True,
    ) -> _LockType: ...
    def pubsub(self, *, shard_hint: Any = ..., ignore_subscribe_messages: bool = ...) -> PubSub: ...
    def execute_command(self, *args, **options: _CommandOptions): ...
    def parse_response(self, connection, command_name, **options: _ParseResponseOptions): ...
    def monitor(self) -> Monitor: ...
    def __enter__(self) -> Redis[_StrType]: ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None
    ) -> None: ...
    def __del__(self) -> None: ...
    def close(self) -> None: ...
    def client(self) -> Redis[_StrType]: ...

StrictRedis = Redis

class PubSub:
    PUBLISH_MESSAGE_TYPES: ClassVar[tuple[str, str]]
    UNSUBSCRIBE_MESSAGE_TYPES: ClassVar[tuple[str, str]]
    HEALTH_CHECK_MESSAGE: ClassVar[str]
    connection_pool: Any
    shard_hint: Any
    ignore_subscribe_messages: Any
    connection: Any
    subscribed_event: threading.Event
    encoder: Any
    health_check_response_b: bytes
    health_check_response: list[str] | list[bytes]
    def __init__(
        self,
        connection_pool,
        shard_hint: Incomplete | None = None,
        ignore_subscribe_messages: bool = False,
        encoder: Incomplete | None = None,
    ) -> None: ...
    def __enter__(self) -> Self: ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None
    ) -> None: ...
    def __del__(self): ...
    channels: Any
    patterns: Any
    def reset(self): ...
    def close(self) -> None: ...
    def on_connect(self, connection): ...
    @property
    def subscribed(self): ...
    def execute_command(self, *args): ...
    def clean_health_check_responses(self) -> None: ...
    def parse_response(self, block: bool = True, timeout: float = 0): ...
    def is_health_check_response(self, response) -> bool: ...
    def check_health(self) -> None: ...
    def psubscribe(self, *args: _Key, **kwargs: Callable[[Any], None]): ...
    def punsubscribe(self, *args: _Key) -> None: ...
    def subscribe(self, *args: _Key, **kwargs: Callable[[Any], None]) -> None: ...
    def unsubscribe(self, *args: _Key) -> None: ...
    def listen(self): ...
    def get_message(self, ignore_subscribe_messages: bool = False, timeout: float = 0.0) -> dict[str, Any] | None: ...
    def handle_message(self, response, ignore_subscribe_messages: bool = False) -> dict[str, Any] | None: ...
    def run_in_thread(self, sleep_time: float = 0, daemon: bool = False, exception_handler: Incomplete | None = None): ...
    def ping(self, message: _Value | None = None) -> None: ...

class PubSubWorkerThread(threading.Thread):
    daemon: Any
    pubsub: Any
    sleep_time: Any
    exception_handler: Any
    def __init__(self, pubsub, sleep_time, daemon: bool = False, exception_handler: Incomplete | None = None) -> None: ...
    def run(self) -> None: ...
    def stop(self) -> None: ...

class Pipeline(Redis[_StrType]):
    UNWATCH_COMMANDS: Any
    connection_pool: Any
    connection: Any
    response_callbacks: Any
    transaction: bool
    shard_hint: Any
    watching: bool

    command_stack: Any
    scripts: Any
    explicit_transaction: Any
    def __init__(self, connection_pool, response_callbacks, transaction, shard_hint) -> None: ...
    def __enter__(self) -> Pipeline[_StrType]: ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None
    ) -> None: ...
    def __del__(self) -> None: ...
    def __len__(self) -> int: ...
    def __bool__(self) -> bool: ...
    def discard(self) -> None: ...
    def reset(self) -> None: ...
    def multi(self) -> None: ...
    def execute_command(self, *args, **options): ...
    def immediate_execute_command(self, *args, **options): ...
    def pipeline_execute_command(self, *args, **options): ...
    def raise_first_error(self, commands, response): ...
    def annotate_exception(self, exception, number, command): ...
    def parse_response(self, connection, command_name, **options): ...
    def load_scripts(self): ...
    def execute(self, raise_on_error: bool = True) -> list[Any]: ...
    def watch(self, *names: _Key) -> bool: ...
    def unwatch(self) -> bool: ...
    # in the Redis implementation, the following methods are inherited from client.
    def set_response_callback(self, command, callback): ...
    def pipeline(self, transaction: bool = True, shard_hint: Any = None) -> Pipeline[_StrType]: ...
    def acl_cat(self, category: str | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def acl_deluser(self, username: str) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def acl_genpass(self, bits: int | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def acl_getuser(self, username: str) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def acl_list(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def acl_load(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def acl_setuser(  # type: ignore[override]
        self,
        username: str,
        enabled: bool = False,
        nopass: bool = False,
        passwords: Sequence[str] | None = None,
        hashed_passwords: Sequence[str] | None = None,
        categories: Sequence[str] | None = None,
        commands: Sequence[str] | None = None,
        keys: Sequence[str] | None = None,
        channels: Iterable[ChannelT] | None = None,
        selectors: Iterable[tuple[str, KeyT]] | None = None,
        reset: bool = False,
        reset_keys: bool = False,
        reset_channels: bool = False,
        reset_passwords: bool = False,
        **kwargs: _CommandOptions,
    ) -> Pipeline[_StrType]: ...
    def acl_users(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def acl_whoami(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def bgrewriteaof(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def bgsave(self, schedule: bool = True) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def client_id(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def client_kill(self, address: str) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def client_list(self, _type: str | None = None, client_id: list[str] = []) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def client_getname(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def client_setname(self, name: str) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def readwrite(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def readonly(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def config_get(self, pattern: PatternT = "*", *args: PatternT, **kwargs: _CommandOptions) -> Pipeline[_StrType]: ...
    def config_set(
        self, name: KeyT, value: EncodableT, *args: KeyT | EncodableT, **kwargs: _CommandOptions
    ) -> Pipeline[_StrType]: ...
    def config_resetstat(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def config_rewrite(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def dbsize(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def debug_object(self, key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def echo(self, value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def flushall(self, asynchronous: bool = False) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def flushdb(self, asynchronous: bool = False) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def info(self, section: _Key | None = None, *args: _Key, **kwargs: _CommandOptions) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def lastsave(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def object(self, infotype, key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def ping(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def save(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sentinel_get_master_addr_by_name(self, service_name) -> Pipeline[_StrType]: ...
    def sentinel_master(self, service_name) -> Pipeline[_StrType]: ...
    def sentinel_masters(self) -> Pipeline[_StrType]: ...
    def sentinel_monitor(self, name, ip, port, quorum) -> Pipeline[_StrType]: ...
    def sentinel_remove(self, name) -> Pipeline[_StrType]: ...
    def sentinel_sentinels(self, service_name) -> Pipeline[_StrType]: ...
    def sentinel_set(self, name, option, value) -> Pipeline[_StrType]: ...
    def sentinel_slaves(self, service_name) -> Pipeline[_StrType]: ...
    def slaveof(self, host=None, port=None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def slowlog_get(self, num=None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def slowlog_len(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def slowlog_reset(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def time(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def append(self, key, value) -> Pipeline[_StrType]: ...
    def bitcount(self, key: _Key, start: int | None = None, end: int | None = None, mode: str | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def bitop(self, operation, dest, *keys) -> Pipeline[_StrType]: ...
    def bitpos(self, key, bit, start=None, end=None, mode: str | None = None) -> Pipeline[_StrType]: ...
    def decr(self, name, amount=1) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def delete(self, *names: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def __delitem__(self, _Key) -> None: ...
    def dump(self, name) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def exists(self, *names: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def __contains__(self, *names: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def expire(self, name: _Key, time: int | timedelta, nx: bool = False, xx: bool = False, gt: bool = False, lt: bool = False) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def expireat(
        self, name, when, nx: bool = False, xx: bool = False, gt: bool = False, lt: bool = False
    ) -> Pipeline[_StrType]: ...
    def get(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def __getitem__(self, name) -> Pipeline[_StrType]: ...
    def getbit(self, name: _Key, offset: int) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def getrange(self, key, start, end) -> Pipeline[_StrType]: ...
    def getset(self, name, value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def incr(self, name, amount=1) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def incrby(self, name, amount=1) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def incrbyfloat(self, name, amount=1.0) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def keys(self, pattern: _Key = "*") -> Pipeline[_StrType]: ...  # type: ignore[override]
    def mget(self, keys: _Key | Iterable[_Key], *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def mset(self, mapping: Mapping[_Key, _Value]) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def msetnx(self, mapping: Mapping[_Key, _Value]) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def move(self, name: _Key, db: int) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def persist(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def pexpire(self, name: _Key, time: int | timedelta, nx: bool = False, xx: bool = False, gt: bool = False, lt: bool = False) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def pexpireat(self, name: _Key, when: int | datetime, nx: bool = False, xx: bool = False, gt: bool = False, lt: bool = False) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def psetex(self, name, time_ms, value) -> Pipeline[_StrType]: ...
    def pttl(self, name) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def randomkey(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def rename(self, src, dst) -> Pipeline[_StrType]: ...
    def renamenx(self, src, dst) -> Pipeline[_StrType]: ...
    def restore(
        self,
        name,
        ttl,
        value,
        replace: bool = False,
        absttl: bool = False,
        idletime: Incomplete | None = None,
        frequency: Incomplete | None = None,
    ) -> Pipeline[_StrType]: ...
    def set(  # type: ignore[override]
        self,
        name: _Key,
        value: _Value,
        ex: None | int | timedelta = None,
        px: None | int | timedelta = None,
        nx: bool = False,
        xx: bool = False,
        keepttl: bool = False,
        get: bool = False,
        exat: Incomplete | None = None,
        pxat: Incomplete | None = None,
    ) -> Pipeline[_StrType]: ...
    def __setitem__(self, name, value) -> None: ...
    def setbit(self, name: _Key, offset: int, value: int) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def setex(self, name: _Key, time: int | timedelta, value: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def setnx(self, name, value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def setrange(self, name, offset, value) -> Pipeline[_StrType]: ...
    def strlen(self, name) -> Pipeline[_StrType]: ...
    def substr(self, name, start, end=-1) -> Pipeline[_StrType]: ...
    def ttl(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def type(self, name) -> Pipeline[_StrType]: ...
    def unlink(self, *names: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def blmove(  # type: ignore[override]
        self,
        first_list: _Key,
        second_list: _Key,
        timeout: float,
        src: Literal["LEFT", "RIGHT"] = "LEFT",
        dest: Literal["LEFT", "RIGHT"] = "RIGHT",
    ) -> Pipeline[_StrType]: ...
    def blpop(self, keys: _Value | Iterable[_Value], timeout: float = 0) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def brpop(self, keys: _Value | Iterable[_Value], timeout: float = 0) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def brpoplpush(self, src, dst, timeout=0) -> Pipeline[_StrType]: ...
    def lindex(self, name: _Key, index: int) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def linsert(  # type: ignore[override]
        self, name: _Key, where: Literal["BEFORE", "AFTER", "before", "after"], refvalue: _Value, value: _Value
    ) -> Pipeline[_StrType]: ...
    def llen(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def lmove(  # type: ignore[override]
        self,
        first_list: _Key,
        second_list: _Key,
        src: Literal["LEFT", "RIGHT"] = "LEFT",
        dest: Literal["LEFT", "RIGHT"] = "RIGHT",
    ) -> Pipeline[_StrType]: ...
    def lpop(self, name, count: int | None = None) -> Pipeline[_StrType]: ...
    def lpush(self, name: _Value, *values: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def lpushx(self, name, value) -> Pipeline[_StrType]: ...
    def lrange(self, name: _Key, start: int, end: int) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def lrem(self, name: _Key, count: int, value: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def lset(self, name: _Key, index: int, value: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def ltrim(self, name: _Key, start: int, end: int) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def rpop(self, name, count: int | None = None) -> Pipeline[_StrType]: ...
    def rpoplpush(self, src, dst) -> Pipeline[_StrType]: ...
    def rpush(self, name: _Value, *values: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def rpushx(self, name, value) -> Pipeline[_StrType]: ...
    def sort(  # type: ignore[override]
        self,
        name: _Key,
        start: int | None = None,
        num: int | None = None,
        by: _Key | None = None,
        get: _Key | Sequence[_Key] | None = None,
        desc: bool = False,
        alpha: bool = False,
        store: _Key | None = None,
        groups: bool = False,
    ) -> Pipeline[_StrType]: ...
    def scan(self, cursor: int = 0, match: _Key | None = None, count: int | None = None, _type: str | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def scan_iter(self, match: _Key | None = None, count: int | None = None, _type: str | None = None) -> Iterator[Any]: ...  # type: ignore[override]
    def sscan(self, name: _Key, cursor: int = 0, match: _Key | None = None, count: int | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sscan_iter(self, name: _Key, match: _Key | None = None, count: int | None = None) -> Iterator[Any]: ...
    def hscan(self, name: _Key, cursor: int = 0, match: _Key | None = None, count: int | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hscan_iter(self, name, match: _Key | None = None, count: int | None = None) -> Iterator[Any]: ...
    def zscan_iter(
        self, name: _Key, match: _Key | None = None, count: int | None = None, score_cast_func: Callable[[_StrType], Any] = ...
    ) -> Iterator[Any]: ...
    def sadd(self, name: _Key, *values: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def scard(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sdiff(self, keys: _Key | Iterable[_Key], *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sdiffstore(self, dest: _Key, keys: _Key | Iterable[_Key], *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sinter(self, keys: _Key | Iterable[_Key], *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sinterstore(self, dest: _Key, keys: _Key | Iterable[_Key], *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sismember(self, name: _Key, value: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def smembers(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def smove(self, src: _Key, dst: _Key, value: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def spop(self, name: _Key, count: int | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def srandmember(self, name: _Key, number: int | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def srem(self, name: _Key, *values: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sunion(self, keys: _Key | Iterable[_Key], *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def sunionstore(self, dest: _Key, keys: _Key | Iterable[_Key], *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def xack(self, name, groupname, *ids) -> Pipeline[_StrType]: ...
    def xadd(
        self,
        name,
        fields,
        id="*",
        maxlen=None,
        approximate: bool = True,
        nomkstream: bool = False,
        minid: Incomplete | None = None,
        limit: int | None = None,
    ) -> Pipeline[_StrType]: ...
    def xclaim(
        self,
        name,
        groupname,
        consumername,
        min_idle_time,
        message_ids,
        idle=None,
        time=None,
        retrycount=None,
        force=False,
        justid=False,
    ) -> Pipeline[_StrType]: ...
    def xdel(self, name, *ids) -> Pipeline[_StrType]: ...
    def xgroup_create(self, name, groupname, id="$", mkstream=False, entries_read: int | None = None) -> Pipeline[_StrType]: ...
    def xgroup_delconsumer(self, name, groupname, consumername) -> Pipeline[_StrType]: ...
    def xgroup_destroy(self, name, groupname) -> Pipeline[_StrType]: ...
    def xgroup_setid(self, name, groupname, id, entries_read: int | None = None) -> Pipeline[_StrType]: ...
    def xinfo_consumers(self, name, groupname) -> Pipeline[_StrType]: ...
    def xinfo_groups(self, name) -> Pipeline[_StrType]: ...
    def xinfo_stream(self, name, full: bool = False) -> Pipeline[_StrType]: ...
    def xlen(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def xpending(self, name, groupname) -> Pipeline[_StrType]: ...
    def xpending_range(
        self, name: _Key, groupname, min, max, count: int, consumername: Incomplete | None = None, idle: int | None = None
    ) -> Pipeline[_StrType]: ...
    def xrange(self, name, min="-", max="+", count=None) -> Pipeline[_StrType]: ...
    def xread(self, streams, count=None, block=None) -> Pipeline[_StrType]: ...
    def xreadgroup(self, groupname, consumername, streams, count=None, block=None, noack=False) -> Pipeline[_StrType]: ...
    def xrevrange(self, name, max="+", min="-", count=None) -> Pipeline[_StrType]: ...
    def xtrim(
        self, name, maxlen: int | None = None, approximate: bool = True, minid: Incomplete | None = None, limit: int | None = None
    ) -> Pipeline[_StrType]: ...
    def zadd(  # type: ignore[override]
        self,
        name: _Key,
        mapping: Mapping[_Key, _Value],
        nx: bool = False,
        xx: bool = False,
        ch: bool = False,
        incr: bool = False,
        gt: Incomplete | None = False,
        lt: Incomplete | None = False,
    ) -> Pipeline[_StrType]: ...
    def zcard(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zcount(self, name: _Key, min: _Value, max: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zincrby(self, name: _Key, amount: float, value: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zinterstore(self, dest: _Key, keys: Iterable[_Key], aggregate: Literal["SUM", "MIN", "MAX"] | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zlexcount(self, name: _Key, min: _Value, max: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zpopmax(self, name: _Key, count: int | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zpopmin(self, name: _Key, count: int | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def bzpopmax(self, keys: _Key | Iterable[_Key], timeout: float = 0) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def bzpopmin(self, keys: _Key | Iterable[_Key], timeout: float = 0) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zrange(  # type: ignore[override]
        self,
        name: _Key,
        start: int,
        end: int,
        desc: bool = False,
        withscores: bool = False,
        score_cast_func: Callable[[_StrType], Any] = ...,
        byscore: bool = False,
        bylex: bool = False,
        offset: int | None = None,
        num: int | None = None,
    ) -> Pipeline[_StrType]: ...
    def zrangebylex(self, name: _Key, min: _Value, max: _Value, start: int | None = None, num: int | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zrangebyscore(  # type: ignore[override]
        self,
        name: _Key,
        min: _Value,
        max: _Value,
        start: int | None = None,
        num: int | None = None,
        withscores: bool = False,
        score_cast_func: Callable[[_StrType], Any] = ...,
    ) -> Pipeline[_StrType]: ...
    def zrank(self, name: _Key, value: _Value, withscore: bool = False) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zrem(self, name: _Key, *values: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zremrangebylex(self, name: _Key, min: _Value, max: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zremrangebyrank(self, name: _Key, min: _Value, max: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zremrangebyscore(self, name: _Key, min: _Value, max: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zrevrange(  # type: ignore[override]
        self, name: _Key, start: int, end: int, withscores: bool = False, score_cast_func: Callable[[_StrType], Any] = ...
    ) -> Pipeline[_StrType]: ...
    def zrevrangebyscore(  # type: ignore[override]
        self,
        name: _Key,
        max: _Value,
        min: _Value,
        start: int | None = None,
        num: int | None = None,
        withscores: bool = False,
        score_cast_func: Callable[[_StrType], Any] = ...,
    ) -> Pipeline[_StrType]: ...
    def zrevrangebylex(  # type: ignore[override]
        self, name: _Key, max: _Value, min: _Value, start: int | None = None, num: int | None = None
    ) -> Pipeline[_StrType]: ...
    def zrevrank(self, name: _Key, value: _Value, withscore: bool = False) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zscore(self, name: _Key, value: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def zunionstore(self, dest: _Key, keys: Iterable[_Key], aggregate: Literal["SUM", "MIN", "MAX"] | None = None) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def pfadd(self, name: _Key, *values: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def pfcount(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def pfmerge(self, dest: _Key, *sources: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hdel(self, name: _Key, *keys: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hexists(self, name: _Key, key: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hget(self, name: _Key, key: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hgetall(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hincrby(self, name: _Key, key: _Key, amount: int = 1) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hincrbyfloat(self, name: _Key, key: _Key, amount: float = 1.0) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hkeys(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hlen(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    @overload  # type: ignore[override]
    def hset(
        self, name: _Key, key: _Key, value: _Value, mapping: Mapping[_Key, _Value] | None = None, items: Incomplete | None = None
    ) -> Pipeline[_StrType]: ...
    @overload
    def hset(
        self, name: _Key, key: None, value: None, mapping: Mapping[_Key, _Value], items: Incomplete | None = None
    ) -> Pipeline[_StrType]: ...
    @overload
    def hset(self, name: _Key, *, mapping: Mapping[_Key, _Value], items: Incomplete | None = None) -> Pipeline[_StrType]: ...
    def hsetnx(self, name: _Key, key: _Key, value: _Value) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hmset(self, name: _Key, mapping: Mapping[_Key, _Value]) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hmget(self, name: _Key, keys: _Key | Iterable[_Key], *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def hvals(self, name: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def publish(self, channel: _Key, message: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def eval(self, script, numkeys, *keys_and_args) -> Pipeline[_StrType]: ...
    def evalsha(self, sha, numkeys, *keys_and_args) -> Pipeline[_StrType]: ...
    def script_exists(self, *args) -> Pipeline[_StrType]: ...
    def script_flush(self, sync_type: Incomplete | None = None) -> Pipeline[_StrType]: ...
    def script_kill(self) -> Pipeline[_StrType]: ...
    def script_load(self, script) -> Pipeline[_StrType]: ...
    def pubsub_channels(self, pattern: _Key = "*") -> Pipeline[_StrType]: ...  # type: ignore[override]
    def pubsub_numsub(self, *args: _Key) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def pubsub_numpat(self) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def monitor(self) -> Monitor: ...
    def cluster(self, cluster_arg: str, *args: Any) -> Pipeline[_StrType]: ...  # type: ignore[override]
    def client(self) -> Any: ...

class Monitor:
    command_re: Pattern[str]
    monitor_re: Pattern[str]
    def __init__(self, connection_pool) -> None: ...
    def __enter__(self) -> Self: ...
    def __exit__(self, *args: Unused) -> None: ...
    def next_command(self) -> dict[str, Any]: ...
    def listen(self) -> Iterable[dict[str, Any]]: ...
