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

from typing import (TYPE_CHECKING, Any, AsyncIterable, Callable, Iterable,
                    Iterator, Optional, Sequence, Tuple, TypeVar, Union)

from grpc._cython import cygrpc

if TYPE_CHECKING:
    from grpc import ServicerContext
    from grpc import StreamStreamClientInterceptor
    from grpc import StreamUnaryClientInterceptor
    from grpc import UnaryStreamClientInterceptor
    from grpc import UnaryUnaryClientInterceptor
    from grpc._server import _Context
    from grpc._server import _RPCState
    from grpc.aio._base_server import ServicerContext as AioServicerContext

RequestType = TypeVar("RequestType")
ResponseType = TypeVar("ResponseType")
SerializingFunction = Callable[[Any], bytes]
DeserializingFunction = Callable[[bytes], Any]
MetadataType = Sequence[Tuple[Union[str, bytes], Union[str, bytes]]]
TuplifiedMetadataType = Tuple[Tuple[Union[str, bytes], Union[str, bytes]]]
ChannelArgumentType = Tuple[Union[str, bytes], Any]
DoneCallbackType = Callable[[Any], None]
NullaryCallbackType = Callable[[], None]
RequestIterableType = Iterable[Any]
ResponseIterableType = Iterable[Any]
GeneralIterableType = Union[Iterable[Any], AsyncIterable[Any]]
UserTag = Callable[[cygrpc.BaseEvent], bool]
IntegratedCallFactory = Callable[
    [
        int,
        bytes,
        None,
        Optional[float],
        Optional[MetadataType],
        Optional[cygrpc.CallCredentials],
        Sequence[Sequence[cygrpc.Operation]],
        UserTag,
        Any,
    ],
    cygrpc.IntegratedCall,
]
ServerTagCallbackType = Tuple[
    Optional["_RPCState"], Sequence[NullaryCallbackType]
]
ServerCallbackTag = Callable[[cygrpc.BaseEvent], ServerTagCallbackType]
ArityAgnosticMethodHandler = Union[
    Callable[[RequestType, '_Context', Callable[[ResponseType], None]],
             ResponseType],
    Callable[[RequestType, '_Context', Callable[[ResponseType], None]],
             Iterator[ResponseType]],
    Callable[
        [Iterator[RequestType], '_Context', Callable[[ResponseType], None]],
        ResponseType], Callable[
            [Iterator[RequestType], '_Context', Callable[[ResponseType], None]],
            Iterator[ResponseType]], Callable[[RequestType, '_Context'],
                                              ResponseType],
    Callable[[RequestType, '_Context'], Iterator[ResponseType]],
    Callable[[Iterator[RequestType], '_Context'],
             ResponseType], Callable[[Iterator[RequestType], '_Context'],
                                     Iterator[ResponseType]]]
InterceptorType = Union['UnaryUnaryClientInterceptor',
                        'UnaryStreamClientInterceptor',
                        'StreamUnaryClientInterceptor',
                        'StreamStreamClientInterceptor']
ServicerContextType = Union['ServicerContext', 'AioServicerContext']
