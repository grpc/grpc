# Copyright 2016 gRPC authors.
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

# TODO: Manually format.

from libc cimport stdlib
from libcpp.vector cimport vector
from libcpp.utility cimport pair
from libcpp.string cimport string

from cython.operator cimport dereference

import warnings

cdef extern from "src/compiler/python_generator.h" namespace "grpc_python_generator":
  cppclass PythonGrpcGenerator:
    pass

cdef extern from "grpc_tools/main.h" namespace "grpc_tools":
  cdef PythonGrpcGenerator g_grpc_code_generator

  int protoc_main(int argc, char *argv[])

def run_main(list args not None):
  cdef char **argv = <char **>stdlib.malloc(len(args)*sizeof(char *))
  for i in range(len(args)):
    argv[i] = args[i]
  return protoc_main(len(args), argv)

grpc_code_generator = int(<long>&g_grpc_code_generator)
