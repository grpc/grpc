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
        readonly AioChannel _channel
        list _references
        object _deadline
        list _done_callbacks

        # Caches the picked event loop, so we can avoid the 30ns overhead each
        # time we need access to the event loop.
        object _loop

        # Flag indicates whether cancel being called or not. Cancellation from
        # Core or peer works perfectly fine with normal procedure. However, we
        # need this flag to clean up resources for cancellation from the
        # application layer. Directly cancelling tasks might cause segfault
        # because Core is holding a pointer for the callback handler.
        bint _is_locally_cancelled

        # Following attributes are used for storing the status of the call and
        # the initial metadata. Waiters are used for pausing the execution of
        # tasks that are asking for one of the field when they are not yet
        # available.
        readonly AioRpcStatus _status
        readonly tuple _initial_metadata
        list _waiters_status
        list _waiters_initial_metadata

        int _send_initial_metadata_flags

    cdef void _create_grpc_call(self, object timeout, bytes method, CallCredentials credentials) except *
    cdef void _set_status(self, AioRpcStatus status) except *
    cdef void _set_initial_metadata(self, tuple initial_metadata) except *
