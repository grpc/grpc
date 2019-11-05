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
"""Desired cancellation status for canceling an ongoing RPC call."""


cdef class AioCancelStatus:

    def __cinit__(self):
        self._code = None
        self._details = None

    def __len__(self):
        if self._code is None:
            return 0
        return 1

    def cancel(self, grpc_status_code code, str details=None):
        self._code = code
        self._details = details

    cpdef object code(self):
        return self._code

    cpdef str details(self):
        return self._details
