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

cdef class _AsyncioResolver:
    cdef:
        object _loop
        grpc_custom_resolver* _grpc_resolver
        object _task_resolve

    @staticmethod
    cdef _AsyncioResolver create(grpc_custom_resolver* grpc_resolver)

    cdef void resolve(self, const char* host, const char* port)
