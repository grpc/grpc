# Copyright 2019 gRPC authors.
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
"""gRPC's Asynchronous Python API.

gRPC Async API objects may only be used on the thread on which they were
created. AsyncIO doesn't provide thread safety for most of its APIs.
"""

from typing import Any, Optional, Sequence, Tuple

import grpc
from grpc._cython.cygrpc import (init_grpc_aio, shutdown_grpc_aio, EOF,
                                 AbortError, BaseError, InternalError,
                                 UsageError)

from ._base_call import (Call, RpcContext, StreamStreamCall, StreamUnaryCall,
                         UnaryStreamCall, UnaryUnaryCall)
from ._base_channel import (Channel, StreamStreamMultiCallable,
                            StreamUnaryMultiCallable, UnaryStreamMultiCallable,
                            UnaryUnaryMultiCallable)
from ._call import AioRpcError
from ._interceptor import (ClientCallDetails, InterceptedUnaryUnaryCall,
                           UnaryUnaryClientInterceptor, ServerInterceptor)
from ._server import server
from ._base_server import Server, ServicerContext
from ._typing import ChannelArgumentType
from ._channel import insecure_channel, secure_channel
from ._metadata import Metadata

###################################  __all__  #################################

__all__ = (
    'init_grpc_aio',
    'shutdown_grpc_aio',
    'AioRpcError',
    'RpcContext',
    'Call',
    'UnaryUnaryCall',
    'UnaryStreamCall',
    'StreamUnaryCall',
    'StreamStreamCall',
    'Channel',
    'UnaryUnaryMultiCallable',
    'UnaryStreamMultiCallable',
    'StreamUnaryMultiCallable',
    'StreamStreamMultiCallable',
    'ClientCallDetails',
    'UnaryUnaryClientInterceptor',
    'InterceptedUnaryUnaryCall',
    'ServerInterceptor',
    'insecure_channel',
    'server',
    'Server',
    'ServicerContext',
    'EOF',
    'secure_channel',
    'AbortError',
    'BaseError',
    'UsageError',
    'InternalError',
    'Metadata',
)
