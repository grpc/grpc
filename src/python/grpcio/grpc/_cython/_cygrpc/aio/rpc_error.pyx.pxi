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


cdef class AioRpcError(Exception):

    def __cinit__(self, tuple initial_metadata, int code, str details, tuple trailing_metadata):
        self._initial_metadata = initial_metadata
        self._code = code
        self._details = details
        self._trailing_metadata = trailing_metadata

    cpdef tuple initial_metadata(self):
        return self._initial_metadata

    cpdef int code(self):
        return self._code

    cpdef str details(self):
        return self._details

    cpdef tuple trailing_metadata(self):
        return self._trailing_metadata
