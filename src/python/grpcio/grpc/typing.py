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

if TYPE_CHECKING:
    class CygrpcChannelCredentials: ...
    class CygrpcCallCredentials: ...
    class CygrpcServerCredentials: ...
    class CygrpcServerCertificateConfig: ...
    class CygrpcBaseEvent: ...
else:
    CygrpcChannelCredentials = cygrpc.ChannelCredentials
    CygrpcCallCredentials = cygrpc.CallCredentials
    CygrpcServerCredentials = cygrpc.ServerCredentials
    CygrpcServerCertificateConfig = cygrpc.ServerCertificateConfig
    CygrpcBaseEvent = cygrpc.BaseEvent

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
    Optional["_RPCState"], Sequence[NullaryCallbackType]
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
