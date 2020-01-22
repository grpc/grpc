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


cdef class _AioCall(GrpcCallWrapper):
    cdef:
        AioChannel _channel
        list _references
        # Caches the picked event loop, so we can avoid the 30ns overhead each
        # time we need access to the event loop.
        object _loop

        # Flag indicates whether cancel being called or not. Cancellation from
        # Core or peer works perfectly fine with normal procedure. However, we
        # need this flag to clean up resources for cancellation from the
        # application layer. Directly cancelling tasks might cause segfault
        # because Core is holding a pointer for the callback handler.
        bint _is_locally_cancelled

        object _deadline

    cdef void _create_grpc_call(self, object timeout, bytes method, CallCredentials credentials) except *
