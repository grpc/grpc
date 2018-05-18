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

cimport cpython


cdef void* _copy_pointer(void* pointer):
  return pointer


cdef void _destroy_pointer(void* pointer):
  pass


cdef int _compare_pointer(void* first_pointer, void* second_pointer):
  if first_pointer < second_pointer:
    return -1
  elif first_pointer > second_pointer:
    return 1
  else:
    return 0


cdef class _ArgumentProcessor:

  cdef void c(self, argument, grpc_arg_pointer_vtable *vtable, references):
    key, value = argument
    cdef bytes encoded_key = _encode(key)
    if encoded_key is not key:
      references.append(encoded_key)
    self.c_argument.key = encoded_key
    if isinstance(value, int):
      self.c_argument.type = GRPC_ARG_INTEGER
      self.c_argument.value.integer = value
    elif isinstance(value, (bytes, str, unicode,)):
      self.c_argument.type = GRPC_ARG_STRING
      encoded_value = _encode(value)
      if encoded_value is not value:
        references.append(encoded_value)
      self.c_argument.value.string = encoded_value
    elif hasattr(value, '__int__'):
      # Pointer objects must override __int__() to return
      # the underlying C address (Python ints are word size). The
      # lifecycle of the pointer is fixed to the lifecycle of the
      # python object wrapping it.
      self.c_argument.type = GRPC_ARG_POINTER
      self.c_argument.value.pointer.vtable = vtable
      self.c_argument.value.pointer.address = <void*>(<intptr_t>int(value))
    else:
      raise TypeError(
          'Expected int, bytes, or behavior, got {}'.format(type(value)))


cdef class _ArgumentsProcessor:

  def __cinit__(self, arguments):
    self._arguments = () if arguments is None else tuple(arguments)
    self._argument_processors = []
    self._references = []

  cdef grpc_channel_args *c(self, grpc_arg_pointer_vtable *vtable):
    self._c_arguments.arguments_length = len(self._arguments)
    if self._c_arguments.arguments_length == 0:
      return NULL
    else:
      self._c_arguments.arguments = <grpc_arg *>gpr_malloc(
          self._c_arguments.arguments_length * sizeof(grpc_arg))
      for index, argument in enumerate(self._arguments):
        argument_processor = _ArgumentProcessor()
        argument_processor.c(argument, vtable, self._references)
        self._c_arguments.arguments[index] = argument_processor.c_argument
        self._argument_processors.append(argument_processor)
      return &self._c_arguments

  cdef un_c(self):
    if self._arguments:
      gpr_free(self._c_arguments.arguments)
