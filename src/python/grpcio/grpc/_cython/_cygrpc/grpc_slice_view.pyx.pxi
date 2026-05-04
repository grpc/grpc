# Copyright 2026 gRPC authors.
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

from cpython.buffer cimport PyBuffer_FillInfo

cdef class GrpcSliceView:
  cdef grpc_slice _slice
  cdef bint _initialized

  def __cinit__(self):
    self._initialized = False

  cdef void set_slice(self, grpc_slice slice) except *:
    self._slice = grpc_slice_ref(slice)
    self._initialized = True

  def __dealloc__(self):
    if self._initialized:
      grpc_slice_unref(self._slice)

  def __getbuffer__(self, Py_buffer *buffer, int flags):
    if not self._initialized:
      raise ValueError("Buffer not initialized")
    cdef void *start = grpc_slice_start_ptr(self._slice)
    cdef size_t length = grpc_slice_length(self._slice)
    
    PyBuffer_FillInfo(buffer, self, start, length, 1, flags)

  def __releasebuffer__(self, Py_buffer *buffer):
    pass
