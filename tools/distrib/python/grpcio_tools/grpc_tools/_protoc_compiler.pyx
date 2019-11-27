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

from libc cimport stdlib
from libcpp.map cimport map
from libcpp.vector cimport vector
from libcpp.string cimport string

import warnings

cdef extern from "grpc_tools/main.h":
  cppclass cProtocError "ProtocError":
    string filename
    int line
    int column
    string message

  cppclass cProtocWarning "ProtocWarning":
    string filename
    int line
    int column
    string message

  int protoc_main(int argc, char *argv[])
  int protoc_in_memory(char* protobuf_path, char* include_path, map[string, string]* files_out, vector[cProtocError]* errors, vector[cProtocWarning]* wrnings) except +

def run_main(list args not None):
  cdef char **argv = <char **>stdlib.malloc(len(args)*sizeof(char *))
  for i in range(len(args)):
    argv[i] = args[i]
  return protoc_main(len(args), argv)

class ProtocError(Exception):
    def __init__(self, filename, line, column, message):
        self.filename = filename
        self.line = line
        self.column = column
        self.message = message

    def __repr__(self):
        return "ProtocError(filename=\"{}\", line={}, column={}, message=\"{}\")".format(self.filename, self.line, self.column, self.message)

    # TODO: Maybe come up with something better than this
    __str__ = __repr__

class ProtocWarning(Warning):
    def __init__(self, filename, line, column, message):
        self.filename = filename
        self.line = line
        self.column = column
        self.message = message

    def __repr__(self):
        return "ProtocWarning(filename=\"{}\", line={}, column={}, message=\"{}\")".format(self.filename, self.line, self.column, self.message)

    # TODO: Maybe come up with something better than this
    __str__ = __repr__

cdef _c_protoc_error_to_protoc_error(cProtocError c_protoc_error):
    return ProtocError(c_protoc_error.filename, c_protoc_error.line, c_protoc_error.column, c_protoc_error.message)

cdef _c_protoc_warning_to_protoc_warning(cProtocWarning c_protoc_warning):
    return ProtocWarning(c_protoc_warning.filename, c_protoc_warning.line, c_protoc_warning.column, c_protoc_warning.message)

def get_protos(bytes protobuf_path, bytes include_path):
  cdef map[string, string] files
  cdef vector[cProtocError] errors
  # NOTE: Abbreviated name used to shadowing of the module name.
  cdef vector[cProtocWarning] wrnings
  return_value = protoc_in_memory(protobuf_path, include_path, &files, &errors, &wrnings)
  for warning in wrnings:
      warnings.warn(_c_protoc_warning_to_protoc_warning(warning))
  if return_value != 0:
    if errors.size() != 0:
       py_errors = [_c_protoc_error_to_protoc_error(c_error) for c_error in errors]
       # TODO: Come up with a good system for printing multiple errors from
       # protoc.
       raise Exception(py_errors)
    raise Exception("An unknown error occurred while compiling {}".format(protobuf_path))

  return files

