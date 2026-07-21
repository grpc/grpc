# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""gRPC's Python API type stubs."""

import abc
import enum
from types import TracebackType
from typing import (
    Any,
    Callable,
    ContextManager,
    Generic,
    Iterable,
    Iterator,
    Mapping,
    Optional,
    Protocol,
    Sequence,
    Tuple,
    TypeVar,
    Union,
    runtime_checkable,
)

from grpc import _common as _common
from grpc import _compression as _compression
from grpc import _grpcio_metadata as _grpcio_metadata
from grpc import _interceptor as _interceptor
from grpc import _observability as _observability
from grpc import _simple_stubs as _simple_stubs
from grpc import _utilities as _utilities
from grpc import aio as aio
from grpc import beta as beta
from grpc import experimental as experimental
from grpc import framework as framework
from grpc._cython import cygrpc as _cygrpc
from grpc._runtime_protos import protos as protos
from grpc._runtime_protos import protos_and_services as protos_and_services
from grpc._runtime_protos import services as services

__version__: str

RequestType = TypeVar("RequestType")
ResponseType = TypeVar("ResponseType")
MetadataType = Sequence[Tuple[str, Union[str, bytes]]]


############################## Future Interface  ###############################

class FutureTimeoutError(Exception): ...

class FutureCancelledError(Exception): ...

class Future(abc.ABC, Generic[ResponseType]):
    @abc.abstractmethod
    def cancel(self) -> bool: ...
    @abc.abstractmethod
    def cancelled(self) -> bool: ...
    @abc.abstractmethod
    def running(self) -> bool: ...
    @abc.abstractmethod
    def done(self) -> bool: ...
    @abc.abstractmethod
    def result(self, timeout: Optional[float] = None) -> ResponseType: ...
    @abc.abstractmethod
    def exception(self, timeout: Optional[float] = None) -> Optional[Exception]: ...
    @abc.abstractmethod
    def traceback(self, timeout: Optional[float] = None) -> Optional[TracebackType]: ...
    @abc.abstractmethod
    def add_done_callback(self, fn: Callable[[Future[ResponseType]], Any]) -> None: ...

################################  gRPC Enums  ##################################

@enum.unique
class ChannelConnectivity(enum.Enum):
    IDLE = ...
    CONNECTING = ...
    READY = ...
    TRANSIENT_FAILURE = ...
    SHUTDOWN = ...

@enum.unique
class StatusCode(enum.Enum):
    OK = ...
    CANCELLED = ...
    UNKNOWN = ...
    INVALID_ARGUMENT = ...
    DEADLINE_EXCEEDED = ...
    NOT_FOUND = ...
    ALREADY_EXISTS = ...
    PERMISSION_DENIED = ...
    RESOURCE_EXHAUSTED = ...
    FAILED_PRECONDITION = ...
    ABORTED = ...
    OUT_OF_RANGE = ...
    UNIMPLEMENTED = ...
    INTERNAL = ...
    UNAVAILABLE = ...
    DATA_LOSS = ...
    UNAUTHENTICATED = ...

#############################  gRPC Status  ################################

class Status(abc.ABC):
    code: StatusCode
    details: str
    trailing_metadata: MetadataType

#############################  gRPC Exceptions  ################################

class RpcError(Exception): ...

##############################  Shared Context  ################################

class RpcContext(abc.ABC):
    @abc.abstractmethod
    def is_active(self) -> bool: ...
    @abc.abstractmethod
    def time_remaining(self) -> Optional[float]: ...
    @abc.abstractmethod
    def cancel(self) -> None: ...
    @abc.abstractmethod
    def add_callback(self, callback: Callable[[], Any]) -> bool: ...

#########################  Invocation-Side Context  ################            

class Call(RpcContext, metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def initial_metadata(self) -> Optional[MetadataType]: ...
    @abc.abstractmethod
    def trailing_metadata(self) -> Optional[MetadataType]: ...
    @abc.abstractmethod
    def code(self) -> Optional[StatusCode]: ...
    @abc.abstractmethod
    def details(self) -> Optional[str]: ...

##############  Invocation-Side Interceptor Interfaces & Classes  ##############

class ClientCallDetails(abc.ABC):
    method: Any
    timeout: Optional[float]
    metadata: Any
    credentials: Optional[CallCredentials]
    wait_for_ready: Optional[bool]
    compression: Optional[Compression]


class UnaryUnaryClientInterceptor(abc.ABC):
    @abc.abstractmethod
    def intercept_unary_unary(
        self,
        continuation: Callable[[ClientCallDetails, Any], Any],
        client_call_details: ClientCallDetails,
        request: Any,
    ) -> Any: ...

class UnaryStreamClientInterceptor(abc.ABC):
    @abc.abstractmethod
    def intercept_unary_stream(
        self,
        continuation: Callable[[ClientCallDetails, Any], Any],
        client_call_details: ClientCallDetails,
        request: Any,
    ) -> Any: ...

class StreamUnaryClientInterceptor(abc.ABC):
    @abc.abstractmethod
    def intercept_stream_unary(
        self,
        continuation: Callable[[ClientCallDetails, Any], Any],
        client_call_details: ClientCallDetails,
        request_iterator: Any,
    ) -> Any: ...

class StreamStreamClientInterceptor(abc.ABC):
    @abc.abstractmethod
    def intercept_stream_stream(
        self,
        continuation: Callable[[ClientCallDetails, Any], Any],
        client_call_details: ClientCallDetails,
        request_iterator: Any,
    ) -> Any: ...

############  Authentication & Authorization Interfaces & Classes  #############

class ChannelCredentials:
    _credentials: Any
    def __init__(self, credentials: Any) -> None: ...

class CallCredentials:
    _credentials: Any
    def __init__(self, credentials: Any) -> None: ...

class AuthMetadataContext(abc.ABC):
    service_url: str
    method_name: str

class AuthMetadataPluginCallback(abc.ABC):
    def __call__(
        self,
        metadata: MetadataType,
        error: Optional[Exception],
    ) -> None: ...

class AuthMetadataPlugin(abc.ABC):
    def __call__(
        self,
        context: AuthMetadataContext,
        callback: AuthMetadataPluginCallback,
    ) -> None: ...

class ServerCredentials:
    _credentials: Any
    def __init__(self, credentials: Any) -> None: ...

class ServerCertificateConfiguration:
    _certificate_configuration: Any
    def __init__(self, certificate_configuration: Any) -> None: ...

########################  Multi-Callable Interfaces  ###########################

class UnaryUnaryMultiCallable(abc.ABC, Generic[RequestType, ResponseType]):
    @abc.abstractmethod
    def __call__(
        self,
        request: RequestType,
        timeout: Optional[float] = None,
        metadata: Optional[MetadataType] = None,
        credentials: Optional[CallCredentials] = None,
        wait_for_ready: Optional[bool] = None,
        compression: Optional[Compression] = None,
    ) -> ResponseType: ...
    @abc.abstractmethod
    def with_call(
        self,
        request: RequestType,
        timeout: Optional[float] = None,
        metadata: Optional[MetadataType] = None,
        credentials: Optional[CallCredentials] = None,
        wait_for_ready: Optional[bool] = None,
        compression: Optional[Compression] = None,
    ) -> Tuple[ResponseType, Call]: ...
    @abc.abstractmethod
    def future(
        self,
        request: RequestType,
        timeout: Optional[float] = None,
        metadata: Optional[MetadataType] = None,
        credentials: Optional[CallCredentials] = None,
        wait_for_ready: Optional[bool] = None,
        compression: Optional[Compression] = None,
    ) -> Future[ResponseType]: ...

class UnaryStreamMultiCallable(abc.ABC, Generic[RequestType, ResponseType]):
    @abc.abstractmethod
    def __call__(
        self,
        request: RequestType,
        timeout: Optional[float] = None,
        metadata: Optional[MetadataType] = None,
        credentials: Optional[CallCredentials] = None,
        wait_for_ready: Optional[bool] = None,
        compression: Optional[Compression] = None,
    ) -> Iterator[ResponseType]: ...

class StreamUnaryMultiCallable(abc.ABC, Generic[RequestType, ResponseType]):
    @abc.abstractmethod
    def __call__(
        self,
        request_iterator: Iterable[RequestType],
        timeout: Optional[float] = None,
        metadata: Optional[MetadataType] = None,
        credentials: Optional[CallCredentials] = None,
        wait_for_ready: Optional[bool] = None,
        compression: Optional[Compression] = None,
    ) -> ResponseType: ...
    @abc.abstractmethod
    def with_call(
        self,
        request_iterator: Iterable[RequestType],
        timeout: Optional[float] = None,
        metadata: Optional[MetadataType] = None,
        credentials: Optional[CallCredentials] = None,
        wait_for_ready: Optional[bool] = None,
        compression: Optional[Compression] = None,
    ) -> Tuple[ResponseType, Call]: ...
    @abc.abstractmethod
    def future(
        self,
        request_iterator: Iterable[RequestType],
        timeout: Optional[float] = None,
        metadata: Optional[MetadataType] = None,
        credentials: Optional[CallCredentials] = None,
        wait_for_ready: Optional[bool] = None,
        compression: Optional[Compression] = None,
    ) -> Future[ResponseType]: ...

class StreamStreamMultiCallable(abc.ABC, Generic[RequestType, ResponseType]):
    @abc.abstractmethod
    def __call__(
        self,
        request_iterator: Iterable[RequestType],
        timeout: Optional[float] = None,
        metadata: Optional[MetadataType] = None,
        credentials: Optional[CallCredentials] = None,
        wait_for_ready: Optional[bool] = None,
        compression: Optional[Compression] = None,
    ) -> Iterator[ResponseType]: ...

#############################  Channel Interface  ##############################

class Channel(abc.ABC):
    @abc.abstractmethod
    def subscribe(
        self,
        callback: Callable[[ChannelConnectivity], None],
        try_to_connect: bool = False,
    ) -> None: ...
    @abc.abstractmethod
    def unsubscribe(
        self,
        callback: Callable[[ChannelConnectivity], None],
    ) -> None: ...
    @abc.abstractmethod
    def unary_unary(
        self,
        method: str,
        request_serializer: Optional[Callable[[Any], bytes]] = None,
        response_deserializer: Optional[Callable[[bytes], Any]] = None,
        _registered_method: bool = False,
    ) -> UnaryUnaryMultiCallable[Any, Any]: ...
    @abc.abstractmethod
    def unary_stream(
        self,
        method: str,
        request_serializer: Optional[Callable[[Any], bytes]] = None,
        response_deserializer: Optional[Callable[[bytes], Any]] = None,
        _registered_method: bool = False,
    ) -> UnaryStreamMultiCallable[Any, Any]: ...
    @abc.abstractmethod
    def stream_unary(
        self,
        method: str,
        request_serializer: Optional[Callable[[Any], bytes]] = None,
        response_deserializer: Optional[Callable[[bytes], Any]] = None,
        _registered_method: bool = False,
    ) -> StreamUnaryMultiCallable[Any, Any]: ...
    @abc.abstractmethod
    def stream_stream(
        self,
        method: str,
        request_serializer: Optional[Callable[[Any], bytes]] = None,
        response_deserializer: Optional[Callable[[bytes], Any]] = None,
        _registered_method: bool = False,
    ) -> StreamStreamMultiCallable[Any, Any]: ...
    @abc.abstractmethod
    def close(self) -> None: ...
    def __enter__(self: Channel) -> Channel: ...
    def __exit__(
        self,
        exc_type: Optional[type[BaseException]],
        exc_val: Optional[BaseException],
        exc_tb: Optional[TracebackType],
    ) -> None: ...

##########################  Service-Side Context  ##############################

class ServicerContext(RpcContext, metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def invocation_metadata(self) -> Optional[MetadataType]: ...
    @abc.abstractmethod
    def peer(self) -> str: ...
    @abc.abstractmethod
    def peer_identities(self) -> Optional[Iterable[bytes]]: ...
    @abc.abstractmethod
    def peer_identity_key(self) -> Optional[str]: ...
    @abc.abstractmethod
    def auth_context(self) -> Mapping[str, Iterable[bytes]]: ...
    def set_compression(self, compression: Compression) -> None: ...
    @abc.abstractmethod
    def send_initial_metadata(self, initial_metadata: MetadataType) -> None: ...
    @abc.abstractmethod
    def set_trailing_metadata(self, trailing_metadata: MetadataType) -> None: ...
    def trailing_metadata(self) -> Optional[MetadataType]: ...
    @abc.abstractmethod
    def abort(self, code: StatusCode, details: str) -> None: ...
    @abc.abstractmethod
    def abort_with_status(self, status: Status) -> None: ...
    @abc.abstractmethod
    def set_code(self, code: StatusCode) -> None: ...
    @abc.abstractmethod
    def set_details(self, details: str) -> None: ...
    def code(self) -> Optional[StatusCode]: ...
    def details(self) -> Optional[Union[str, bytes]]: ...
    def disable_next_message_compression(self) -> None: ...

#####################  Service-Side Handler Interfaces  ########################

class RpcMethodHandler(abc.ABC):
    request_streaming: bool
    response_streaming: bool
    request_deserializer: Optional[Callable[[bytes], Any]]
    response_serializer: Optional[Callable[[Any], bytes]]
    unary_unary: Optional[Callable[[Any, ServicerContext], Any]]
    unary_stream: Optional[Callable[[Any, ServicerContext], Iterator[Any]]]
    stream_unary: Optional[Callable[[Iterator[Any], ServicerContext], Any]]
    stream_stream: Optional[Callable[[Iterator[Any], ServicerContext], Iterator[Any]]]

@runtime_checkable
class HandlerCallDetails(Protocol):
    method: str
    invocation_metadata: Any

class GenericRpcHandler(abc.ABC):
    @abc.abstractmethod
    def service(
        self, handler_call_details: HandlerCallDetails
    ) -> Optional[RpcMethodHandler]: ...

class ServiceRpcHandler(GenericRpcHandler, metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def service_name(self) -> str: ...

####################  Service-Side Interceptor Interfaces  #####################

class ServerInterceptor(abc.ABC):
    @abc.abstractmethod
    def intercept_service(
        self,
        continuation: Callable[[HandlerCallDetails], Optional[RpcMethodHandler]],
        handler_call_details: HandlerCallDetails,
    ) -> Optional[RpcMethodHandler]: ...

#############################  Server Interface  ###############################

class Server(abc.ABC):
    @abc.abstractmethod
    def add_generic_rpc_handlers(
        self, generic_rpc_handlers: Iterable[GenericRpcHandler]
    ) -> None: ...
    def add_registered_method_handlers(
        self, service_name: str, method_handlers: Any
    ) -> None: ...
    @abc.abstractmethod
    def add_insecure_port(self, address: str) -> int: ...
    @abc.abstractmethod
    def add_secure_port(
        self, address: str, server_credentials: ServerCredentials
    ) -> int: ...
    @abc.abstractmethod
    def start(self) -> None: ...
    @abc.abstractmethod
    def stop(self, grace: Optional[float]) -> Any: ...
    def wait_for_termination(self, timeout: Optional[float] = None) -> bool: ...

#################################  Functions    ################################

def unary_unary_rpc_method_handler(
    behavior: Callable[[Any, ServicerContext], Any],
    request_deserializer: Optional[Callable[[bytes], Any]] = None,
    response_serializer: Optional[Callable[[Any], bytes]] = None,
) -> RpcMethodHandler: ...

def unary_stream_rpc_method_handler(
    behavior: Callable[[Any, ServicerContext], Iterator[Any]],
    request_deserializer: Optional[Callable[[bytes], Any]] = None,
    response_serializer: Optional[Callable[[Any], bytes]] = None,
) -> RpcMethodHandler: ...

def stream_unary_rpc_method_handler(
    behavior: Callable[[Iterator[Any], ServicerContext], Any],
    request_deserializer: Optional[Callable[[bytes], Any]] = None,
    response_serializer: Optional[Callable[[Any], bytes]] = None,
) -> RpcMethodHandler: ...

def stream_stream_rpc_method_handler(
    behavior: Callable[[Iterator[Any], ServicerContext], Iterator[Any]],
    request_deserializer: Optional[Callable[[bytes], Any]] = None,
    response_serializer: Optional[Callable[[Any], bytes]] = None,
) -> RpcMethodHandler: ...

def method_handlers_generic_handler(
    service: str, method_handlers: Mapping[str, RpcMethodHandler]
) -> GenericRpcHandler: ...

def ssl_channel_credentials(
    root_certificates: Optional[bytes] = None,
    private_key: Optional[bytes] = None,
    certificate_chain: Optional[bytes] = None,
) -> ChannelCredentials: ...

def xds_channel_credentials(
    fallback_credentials: Optional[ChannelCredentials] = None,
) -> ChannelCredentials: ...

def metadata_call_credentials(
    metadata_plugin: AuthMetadataPlugin, name: Optional[str] = None
) -> CallCredentials: ...

def access_token_call_credentials(access_token: str) -> CallCredentials: ...

def composite_call_credentials(*call_credentials: CallCredentials) -> CallCredentials: ...

def composite_channel_credentials(
    channel_credentials: ChannelCredentials, *call_credentials: CallCredentials
) -> ChannelCredentials: ...

def ssl_server_credentials(
    private_key_certificate_chain_pairs: Sequence[Tuple[bytes, bytes]],
    root_certificates: Optional[bytes] = None,
    require_client_auth: bool = False,
) -> ServerCredentials: ...

def xds_server_credentials(
    fallback_credentials: ServerCredentials,
) -> ServerCredentials: ...

def insecure_server_credentials() -> ServerCredentials: ...

def ssl_server_certificate_configuration(
    private_key_certificate_chain_pairs: Sequence[Tuple[bytes, bytes]],
    root_certificates: Optional[bytes] = None,
) -> ServerCertificateConfiguration: ...

def dynamic_ssl_server_credentials(
    initial_certificate_configuration: ServerCertificateConfiguration,
    certificate_configuration_fetcher: Callable[[], Optional[ServerCertificateConfiguration]],
    require_client_authentication: bool = False,
) -> ServerCredentials: ...

@enum.unique
class LocalConnectionType(enum.Enum):
    UDS = ...
    LOCAL_TCP = ...

def local_channel_credentials(
    local_connect_type: LocalConnectionType = LocalConnectionType.LOCAL_TCP,
) -> ChannelCredentials: ...

def local_server_credentials(
    local_connect_type: LocalConnectionType = LocalConnectionType.LOCAL_TCP,
) -> ServerCredentials: ...

def alts_channel_credentials(
    service_accounts: Optional[Sequence[str]] = None,
) -> ChannelCredentials: ...

def alts_server_credentials() -> ServerCredentials: ...

def compute_engine_channel_credentials(
    call_credentials: CallCredentials,
) -> ChannelCredentials: ...

def channel_ready_future(channel: Channel) -> Future[None]: ...

def insecure_channel(
    target: str,
    options: Optional[Sequence[Tuple[str, Any]]] = None,
    compression: Optional[Compression] = None,
) -> Channel: ...

def secure_channel(
    target: str,
    credentials: ChannelCredentials,
    options: Optional[Sequence[Tuple[str, Any]]] = None,
    compression: Optional[Compression] = None,
) -> Channel: ...

def intercept_channel(
    channel: Channel,
    *interceptors: Union[
        UnaryUnaryClientInterceptor,
        UnaryStreamClientInterceptor,
        StreamUnaryClientInterceptor,
        StreamStreamClientInterceptor,
    ],
) -> Channel: ...

def server(
    thread_pool: Any,
    handlers: Optional[Sequence[GenericRpcHandler]] = None,
    interceptors: Optional[Sequence[ServerInterceptor]] = None,
    options: Optional[Sequence[Tuple[str, Any]]] = None,
    maximum_concurrent_rpcs: Optional[int] = None,
    compression: Optional[Compression] = None,
    xds: bool = False,
) -> Server: ...

def _create_servicer_context(
    rpc_event: Any, state: Any, request_deserializer: Any
) -> ContextManager[ServicerContext]: ...

@enum.unique
class Compression(enum.IntEnum):
    NoCompression = ...
    Deflate = ...
    Gzip = ...

__all__ = (
    "AuthMetadataContext",
    "AuthMetadataPlugin",
    "AuthMetadataPluginCallback",
    "Call",
    "CallCredentials",
    "Channel",
    "ChannelConnectivity",
    "ChannelCredentials",
    "ClientCallDetails",
    "Compression",
    "Future",
    "FutureCancelledError",
    "FutureTimeoutError",
    "GenericRpcHandler",
    "HandlerCallDetails",
    "LocalConnectionType",
    "RpcContext",
    "RpcError",
    "RpcMethodHandler",
    "Server",
    "ServerCertificateConfiguration",
    "ServerCredentials",
    "ServerInterceptor",
    "ServiceRpcHandler",
    "ServicerContext",
    "Status",
    "StatusCode",
    "StreamStreamClientInterceptor",
    "StreamStreamMultiCallable",
    "StreamUnaryClientInterceptor",
    "StreamUnaryMultiCallable",
    "UnaryStreamClientInterceptor",
    "UnaryStreamMultiCallable",
    "UnaryUnaryClientInterceptor",
    "UnaryUnaryMultiCallable",
    "_common",
    "_compression",
    "_cygrpc",
    "_grpcio_metadata",
    "_interceptor",
    "_observability",
    "_simple_stubs",
    "_utilities",
    "access_token_call_credentials",
    "alts_channel_credentials",
    "alts_server_credentials",
    "channel_ready_future",
    "composite_call_credentials",
    "composite_channel_credentials",
    "compute_engine_channel_credentials",
    "dynamic_ssl_server_credentials",
    "insecure_channel",
    "insecure_server_credentials",
    "intercept_channel",
    "local_channel_credentials",
    "local_server_credentials",
    "metadata_call_credentials",
    "method_handlers_generic_handler",
    "protos",
    "protos_and_services",
    "secure_channel",
    "server",
    "services",
    "ssl_channel_credentials",
    "ssl_server_certificate_configuration",
    "ssl_server_credentials",
    "stream_stream_rpc_method_handler",
    "stream_unary_rpc_method_handler",
    "unary_stream_rpc_method_handler",
    "unary_unary_rpc_method_handler",
    "xds_channel_credentials",
    "xds_server_credentials",
)

def __getattr__(name: str) -> Any: ...




