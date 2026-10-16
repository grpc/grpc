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


cdef class AioRpcStatus(Exception):

    # The final status of gRPC is represented by three trailing metadata:
    # `grpc-status`, `grpc-status-message`, and `grpc-status-details`.
    def __cinit__(self,
                  grpc_status_code code,
                  str details,
                  tuple trailing_metadata,
                  str debug_error_string):
        self._code = code
        self._details = details
        self._trailing_metadata = trailing_metadata
        self._debug_error_string = debug_error_string

    cpdef grpc_status_code code(self):
        return self._code

    cpdef str details(self):
        return self._details

    cpdef tuple trailing_metadata(self):
        return self._trailing_metadata

    cpdef str debug_error_string(self):
        return self._debug_error_string

    cdef grpc_status_code c_code(self):
        return <grpc_status_code>self._code
