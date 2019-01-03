# Copyright 2018 The gRPC Authors
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


cdef tuple _wrap_grpc_arg(grpc_arg arg)


cdef grpc_arg _unwrap_grpc_arg(tuple wrapped_arg)


cdef class _ChannelArg:

  cdef grpc_arg c_argument

  cdef void c(self, argument, grpc_arg_pointer_vtable *vtable, references) except *


cdef class _ChannelArgs:

  cdef readonly tuple _arguments
  cdef list _channel_args
  cdef readonly list _references
  cdef grpc_channel_args _c_arguments

  cdef void _c(self, grpc_arg_pointer_vtable *vtable) except *
  cdef grpc_channel_args *c_args(self) except *

  @staticmethod
  cdef _ChannelArgs from_args(object arguments, grpc_arg_pointer_vtable * vtable)
