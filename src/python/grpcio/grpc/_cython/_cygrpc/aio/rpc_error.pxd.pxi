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
"""Exceptions for the aio version of the RPC calls."""


cdef class _AioRpcError(Exception):
    cdef readonly:
        tuple _initial_metadata
        int _code
        str _details
        tuple _trailing_metadata

    cpdef tuple initial_metadata(self)
    cpdef int code(self)
    cpdef str details(self)
    cpdef tuple trailing_metadata(self)
