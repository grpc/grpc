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


cdef class _GrpcArgWrapper:

  cdef grpc_arg arg


cdef tuple _wrap_grpc_arg(grpc_arg arg):
  wrapped = _GrpcArgWrapper()
  wrapped.arg = arg
  return ("grpc.python._cygrpc._GrpcArgWrapper", wrapped)


cdef grpc_arg _unwrap_grpc_arg(tuple wrapped_arg):
  cdef _GrpcArgWrapper wrapped = wrapped_arg[1]
  return wrapped.arg


cdef class _ChannelArg:

  cdef void c(self, argument, grpc_arg_pointer_vtable *vtable, references) except *:
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
    elif isinstance(value, _GrpcArgWrapper):
      self.c_argument = (<_GrpcArgWrapper>value).arg
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


cdef class _ChannelArgs:

  def __cinit__(self, arguments):
    self._arguments = () if arguments is None else tuple(arguments)
    self._channel_args = []
    self._references = []
    self._c_arguments.arguments = NULL

  cdef void _c(self, grpc_arg_pointer_vtable *vtable) except *:
    self._c_arguments.arguments_length = len(self._arguments)
    if self._c_arguments.arguments_length != 0:
      self._c_arguments.arguments = <grpc_arg *>gpr_malloc(
          self._c_arguments.arguments_length * sizeof(grpc_arg))
      for index, argument in enumerate(self._arguments):
        channel_arg = _ChannelArg()
        channel_arg.c(argument, vtable, self._references)
        self._c_arguments.arguments[index] = channel_arg.c_argument
        self._channel_args.append(channel_arg)

  cdef grpc_channel_args *c_args(self) except *:
    return &self._c_arguments

  def __dealloc__(self):
    if self._c_arguments.arguments != NULL:
      gpr_free(self._c_arguments.arguments)

  @staticmethod
  cdef _ChannelArgs from_args(object arguments, grpc_arg_pointer_vtable * vtable):
    cdef _ChannelArgs channel_args = _ChannelArgs(arguments)
    channel_args._c(vtable)
    return channel_args
