from _typeshed import Incomplete
from collections.abc import Awaitable, Callable, Mapping
from types import TracebackType
from typing import Any, Generic
from typing_extensions import Self

from redis.asyncio.client import ResponseCallbackT
from redis.asyncio.connection import BaseParser, Connection, Encoder
from redis.asyncio.parser import CommandsParser
from redis.client import AbstractRedis
from redis.cluster import AbstractRedisCluster, LoadBalancer

# TODO: add  AsyncRedisClusterCommands stubs
# from redis.commands import AsyncRedisClusterCommands
from redis.commands.core import _StrType
from redis.credentials import CredentialProvider
from redis.retry import Retry
from redis.typing import AnyKeyT, EncodableT, KeyT

# It uses `DefaultParser` in real life, but it is a dynamic base class.
class ClusterParser(BaseParser): ...

class RedisCluster(AbstractRedis, AbstractRedisCluster, Generic[_StrType]):  # TODO: AsyncRedisClusterCommands
    retry: Retry | None
    connection_kwargs: dict[str, Any]
    nodes_manager: NodesManager
    encoder: Encoder
    read_from_replicas: bool
    reinitialize_steps: int
    cluster_error_retry_attempts: int
    reinitialize_counter: int
    commands_parser: CommandsParser
    node_flags: set[str]
    command_flags: dict[str, str]
    response_callbacks: Incomplete
    result_callbacks: dict[str, Callable[[Incomplete, Incomplete], Incomplete]]
    def __init__(
        self,
        host: str | None = None,
        port: str | int = 6379,
        # Cluster related kwargs
        startup_nodes: list[ClusterNode] | None = None,
        require_full_coverage: bool = True,
        read_from_replicas: bool = False,
        reinitialize_steps: int = 5,
        cluster_error_retry_attempts: int = 3,
        connection_error_retry_attempts: int = 3,
        max_connections: int = 2147483648,
        # Client related kwargs
        db: str | int = 0,
        path: str | None = None,
        credential_provider: CredentialProvider | None = None,
        username: str | None = None,
        password: str | None = None,
        client_name: str | None = None,
        # Encoding related kwargs
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        decode_responses: bool = False,
        # Connection related kwargs
        health_check_interval: float = 0,
        socket_connect_timeout: float | None = None,
        socket_keepalive: bool = False,
        socket_keepalive_options: Mapping[int, int | bytes] | None = None,
        socket_timeout: float | None = None,
        retry: Retry | None = None,
        retry_on_error: list[Exception] | None = None,
        # SSL related kwargs
        ssl: bool = False,
        ssl_ca_certs: str | None = None,
        ssl_ca_data: str | None = None,
        ssl_cert_reqs: str = "required",
        ssl_certfile: str | None = None,
        ssl_check_hostname: bool = False,
        ssl_keyfile: str | None = None,
        address_remap: Callable[[str, int], tuple[str, int]] | None = None,
    ) -> None: ...
    async def initialize(self) -> Self: ...
    async def close(self) -> None: ...
    async def __aenter__(self) -> Self: ...
    async def __aexit__(
        self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None
    ) -> None: ...
    def __await__(self) -> Awaitable[Self]: ...
    def __del__(self) -> None: ...
    async def on_connect(self, connection: Connection) -> None: ...
    def get_nodes(self) -> list[ClusterNode]: ...
    def get_primaries(self) -> list[ClusterNode]: ...
    def get_replicas(self) -> list[ClusterNode]: ...
    def get_random_node(self) -> ClusterNode: ...
    def get_default_node(self) -> ClusterNode: ...
    def set_default_node(self, node: ClusterNode) -> None: ...
    def get_node(self, host: str | None = None, port: int | None = None, node_name: str | None = None) -> ClusterNode | None: ...
    def get_node_from_key(self, key: str, replica: bool = False) -> ClusterNode | None: ...
    def keyslot(self, key: EncodableT) -> int: ...
    def get_encoder(self) -> Encoder: ...
    def get_connection_kwargs(self) -> dict[str, Any | None]: ...
    def set_response_callback(self, command: str, callback: ResponseCallbackT) -> None: ...
    async def execute_command(self, *args: EncodableT, **kwargs: Any) -> Any: ...
    def pipeline(self, transaction: Any | None = None, shard_hint: Any | None = None) -> ClusterPipeline[_StrType]: ...
    @classmethod
    def from_url(cls, url: str, **kwargs) -> Self: ...

class ClusterNode:
    host: str
    port: str | int
    name: str
    server_type: str | None
    max_connections: int
    connection_class: type[Connection]
    connection_kwargs: dict[str, Any]
    response_callbacks: dict[Incomplete, Incomplete]
    def __init__(
        self,
        host: str,
        port: str | int,
        server_type: str | None = None,
        *,
        max_connections: int = 2147483648,
        connection_class: type[Connection] = ...,
        **connection_kwargs: Any,
    ) -> None: ...
    def __eq__(self, obj: object) -> bool: ...
    def __del__(self) -> None: ...
    async def disconnect(self) -> None: ...
    def acquire_connection(self) -> Connection: ...
    async def parse_response(self, connection: Connection, command: str, **kwargs: Any) -> Any: ...
    async def execute_command(self, *args: Any, **kwargs: Any) -> Any: ...
    async def execute_pipeline(self, commands: list[PipelineCommand]) -> bool: ...

class NodesManager:
    startup_nodes: dict[str, ClusterNode]
    require_full_coverage: bool
    connection_kwargs: dict[str, Any]
    default_node: ClusterNode | None
    nodes_cache: dict[str, ClusterNode]
    slots_cache: dict[int, list[ClusterNode]]
    read_load_balancer: LoadBalancer
    address_remap: Callable[[str, int], tuple[str, int]] | None
    def __init__(
        self,
        startup_nodes: list[ClusterNode],
        require_full_coverage: bool,
        connection_kwargs: dict[str, Any],
        address_remap: Callable[[str, int], tuple[str, int]] | None = None,
    ) -> None: ...
    def get_node(self, host: str | None = None, port: int | None = None, node_name: str | None = None) -> ClusterNode | None: ...
    def set_nodes(self, old: dict[str, ClusterNode], new: dict[str, ClusterNode], remove_old: bool = False) -> None: ...
    def get_node_from_slot(self, slot: int, read_from_replicas: bool = False) -> ClusterNode: ...
    def get_nodes_by_server_type(self, server_type: str) -> list[ClusterNode]: ...
    async def initialize(self) -> None: ...
    async def close(self, attr: str = "nodes_cache") -> None: ...
    def remap_host_port(self, host: str, port: int) -> tuple[str, int]: ...

class ClusterPipeline(AbstractRedis, AbstractRedisCluster, Generic[_StrType]):  # TODO: AsyncRedisClusterCommands
    def __init__(self, client: RedisCluster[_StrType]) -> None: ...
    async def initialize(self) -> Self: ...
    async def __aenter__(self) -> Self: ...
    async def __aexit__(
        self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None
    ) -> None: ...
    def __await__(self) -> Awaitable[Self]: ...
    def __enter__(self) -> Self: ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None
    ) -> None: ...
    def __bool__(self) -> bool: ...
    def __len__(self) -> int: ...
    def execute_command(self, *args: KeyT | EncodableT, **kwargs: Any) -> Self: ...
    async def execute(self, raise_on_error: bool = True, allow_redirections: bool = True) -> list[Any]: ...
    def mset_nonatomic(self, mapping: Mapping[AnyKeyT, EncodableT]) -> Self: ...

class PipelineCommand:
    args: Any
    kwargs: Any
    position: int
    result: Exception | None | Any
    def __init__(self, position: int, *args: Any, **kwargs: Any) -> None: ...
