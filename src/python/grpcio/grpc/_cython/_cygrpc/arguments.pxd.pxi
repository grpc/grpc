# Copyright 2018 gRPC authors.
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


cdef void* _copy_pointer(void* pointer)


cdef void _destroy_pointer(void* pointer)


cdef int _compare_pointer(void* first_pointer, void* second_pointer)


cdef class _ArgumentProcessor:

  cdef grpc_arg c_argument

  cdef void c(self, argument, grpc_arg_pointer_vtable *vtable, references)


cdef class _ArgumentsProcessor:

  cdef readonly tuple _arguments
  cdef list _argument_processors
  cdef readonly list _references
  cdef grpc_channel_args _c_arguments

  cdef grpc_channel_args *c(self, grpc_arg_pointer_vtable *vtable)
  cdef un_c(self)
