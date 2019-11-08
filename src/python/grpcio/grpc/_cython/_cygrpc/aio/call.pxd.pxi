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

    cdef grpc_call* _create_grpc_call(self, object timeout, bytes method) except *
    cdef void _destroy_grpc_call(self)
