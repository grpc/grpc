# Copyright 2022 gRPC authors.
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
"""Common types for gRPC Sync API"""

from typing import (
    TYPE_CHECKING,
    Any,
    AsyncIterable,
    Callable,
    Iterable,
    Iterator,
    Optional,
    Sequence,
    Tuple,
    TypeVar,
    Union,
)

from grpc._cython import cygrpc

class CygrpcChannelCredentials:
    """Type alias for cygrpc.ChannelCredentials."""
    _cy_creds: cygrpc.ChannelCredentials

    def __init__(self, cy_creds: cygrpc.ChannelCredentials):
        self._cy_creds = cy_creds


class CygrpcCallCredentials:
    """Type alias for cygrpc.CallCredentials."""
    _cy_creds: cygrpc.CallCredentials

    def __init__(self, cy_creds: cygrpc.CallCredentials):
        self._cy_creds = cy_creds


class CygrpcServerCredentials:
    """Type alias for cygrpc.ServerCredentials."""
    _cy_creds: cygrpc.ServerCredentials

    def __init__(self, cy_creds: cygrpc.ServerCredentials):
        self._cy_creds = cy_creds


class CygrpcServerCertificateConfig:
    """Type alias for cygrpc.ServerCertificateConfig."""
    _cy_config: cygrpc.ServerCertificateConfig

    def __init__(self, cy_config: cygrpc.ServerCertificateConfig):
        self._cy_config = cy_config


class CygrpcBaseEvent:
    """Type alias for cygrpc.BaseEvent."""
    _cy_event: cygrpc.BaseEvent

    def __init__(self, cy_event: cygrpc.BaseEvent):
        self._cy_event = cy_event

if TYPE_CHECKING:
    from grpc import ChannelConnectivity
    from grpc import ServicerContext
    from grpc import StreamStreamClientInterceptor
    from grpc import StreamUnaryClientInterceptor
    from grpc import UnaryStreamClientInterceptor
    from grpc import UnaryUnaryClientInterceptor
    from grpc._server import _RPCState

RequestType = TypeVar("RequestType")
ResponseType = TypeVar("ResponseType")
SerializingFunction = Callable[[Any], bytes]
DeserializingFunction = Callable[[bytes], Any]
MetadataType = Sequence[Tuple[str, Union[str, bytes]]]
ChannelArgumentType = Tuple[str, Any]
_DoneCallbackType = Callable[[Any], None]
NullaryCallbackType = Callable[[], None]
_RequestIterableType = Iterable[Any]
_ResponseIterableType = Iterable[Any]
_UserTag = Callable[[cygrpc.BaseEvent], bool]
_IntegratedCallFactory = Callable[
    [
        int,
        bytes,
        Optional[str],
        Optional[float],
        Optional[MetadataType],
        Optional[cygrpc.CallCredentials],
        Sequence[Sequence[cygrpc.Operation]],
        _UserTag,
        Any,
        Optional[int],
    ],
    cygrpc.IntegratedCall,
]
_ServerTagCallbackType = Tuple[
    Optional["_RPCState"], Optional[Sequence[NullaryCallbackType]]
]
_ServerCallbackTag = Callable[[cygrpc.BaseEvent], _ServerTagCallbackType]
ArityAgnosticMethodHandler = Union[
    Callable[
        [RequestType, "ServicerContext", Callable[[ResponseType], None]],
        ResponseType,
    ],
    Callable[
        [RequestType, "ServicerContext", Callable[[ResponseType], None]],
        Iterator[ResponseType],
    ],
    Callable[
        [
            Iterator[RequestType],
            "ServicerContext",
            Callable[[ResponseType], None],
        ],
        ResponseType,
    ],
    Callable[
        [
            Iterator[RequestType],
            "ServicerContext",
            Callable[[ResponseType], None],
        ],
        Iterator[ResponseType],
    ],
    Callable[[RequestType, "ServicerContext"], ResponseType],
    Callable[[RequestType, "ServicerContext"], Iterator[ResponseType]],
    Callable[[Iterator[RequestType], "ServicerContext"], ResponseType],
    Callable[
        [Iterator[RequestType], "ServicerContext"], Iterator[ResponseType]
    ],
]
ClientInterceptor = Union[
    "UnaryUnaryClientInterceptor",
    "UnaryStreamClientInterceptor",
    "StreamUnaryClientInterceptor",
    "StreamStreamClientInterceptor",
]
ConnectivityCallbackType = Callable[["ChannelConnectivity"], None]
UnaryUnaryBehavior = Callable[[RequestType, "ServicerContext"], ResponseType]
UnaryStreamBehavior = Callable[
    [RequestType, "ServicerContext"],
    Union[Iterator[ResponseType], AsyncIterable[ResponseType]],
]
StreamUnaryBehavior = Callable[
    [
        Union[Iterator[RequestType], AsyncIterable[RequestType]],
        "ServicerContext",
    ],
    ResponseType,
]
StreamStreamBehavior = Callable[
    [
        Union[Iterator[RequestType], AsyncIterable[RequestType]],
        "ServicerContext",
    ],
    Union[Iterator[ResponseType], AsyncIterable[ResponseType]],
]
