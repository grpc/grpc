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


cdef class _AioCall:
    cdef:
        AioChannel _channel
        list _references
        GrpcCallWrapper _grpc_call_wrapper
        # Caches the picked event loop, so we can avoid the 30ns overhead each
        # time we need access to the event loop.
        object _loop

        # Streaming call only attributes:
        # 
        # A asyncio.Event that indicates if the status is received on the client side.
        object _status_received
        # A tuple of key value pairs representing the initial metadata sent by peer.
        tuple _initial_metadata

    cdef grpc_call* _create_grpc_call(self, object timeout, bytes method) except *
    cdef void _destroy_grpc_call(self)
    cdef AioRpcStatus _cancel_and_create_status(self, object cancellation_future)
