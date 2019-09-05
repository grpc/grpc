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

# TODO(https://github.com/grpc/grpc/issues/15662): Reform this.
cdef void* _copy_pointer(void* pointer):
  return pointer


# TODO(https://github.com/grpc/grpc/issues/15662): Reform this.
cdef void _destroy_pointer(void* pointer):
  pass


cdef int _compare_pointer(void* first_pointer, void* second_pointer):
  if first_pointer < second_pointer:
    return -1
  elif first_pointer > second_pointer:
    return 1
  else:
    return 0

cdef grpc_arg_pointer_vtable default_vtable
default_vtable.copy = &_copy_pointer
default_vtable.destroy = &_destroy_pointer
default_vtable.cmp = &_compare_pointer
