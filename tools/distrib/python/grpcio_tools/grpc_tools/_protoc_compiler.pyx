# Copyright 2020 The gRPC authors.
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
from libcpp.vector cimport vector
from libcpp.utility cimport pair
from libcpp.string cimport string

from cython.operator cimport dereference

import warnings

cdef extern from "grpc_tools/main.h" namespace "grpc_tools":
  cppclass cProtocError "::grpc_tools::ProtocError":
    string filename
    int line
    int column
    string message

  cppclass cProtocWarning "::grpc_tools::ProtocWarning":
    string filename
    int line
    int column
    string message

  int protoc_main(int argc, char *argv[])
  int protoc_get_protos(char* protobuf_path,
                        vector[string]* include_path,
                        vector[pair[string, string]]* files_out,
                        vector[cProtocError]* errors,
                        vector[cProtocWarning]* wrnings) nogil except +
  int protoc_get_services(char* protobuf_path,
                          vector[string]* include_path,
                          vector[pair[string, string]]* files_out,
                          vector[cProtocError]* errors,
                          vector[cProtocWarning]* wrnings) nogil except +

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
        return "ProtocError(filename=\"{}\", line={}, column={}, message=\"{}\")".format(
                self.filename, self.line, self.column, self.message)

    def __str__(self):
        return "{}:{}:{} error: {}".format(self.filename.decode("ascii"),
                self.line, self.column, self.message.decode("ascii"))

class ProtocWarning(Warning):
    def __init__(self, filename, line, column, message):
        self.filename = filename
        self.line = line
        self.column = column
        self.message = message

    def __repr__(self):
        return "ProtocWarning(filename=\"{}\", line={}, column={}, message=\"{}\")".format(
                self.filename, self.line, self.column, self.message)

    __str__ = __repr__


class ProtocErrors(Exception):
    def __init__(self, errors):
        self._errors = errors

    def errors(self):
        return self._errors

    def __repr__(self):
        return "ProtocErrors[{}]".join(repr(err) for err in self._errors)

    def __str__(self):
        return "\n".join(str(err) for err in self._errors)

cdef _c_protoc_error_to_protoc_error(cProtocError c_protoc_error):
    return ProtocError(c_protoc_error.filename, c_protoc_error.line,
            c_protoc_error.column, c_protoc_error.message)

cdef _c_protoc_warning_to_protoc_warning(cProtocWarning c_protoc_warning):
    return ProtocWarning(c_protoc_warning.filename, c_protoc_warning.line,
            c_protoc_warning.column, c_protoc_warning.message)

cdef _handle_errors(int rc, vector[cProtocError]* errors, vector[cProtocWarning]* wrnings, bytes protobuf_path):
  for warning in dereference(wrnings):
      warnings.warn(_c_protoc_warning_to_protoc_warning(warning))
  if rc != 0:
    if dereference(errors).size() != 0:
       py_errors = [_c_protoc_error_to_protoc_error(c_error)
               for c_error in dereference(errors)]
       raise ProtocErrors(py_errors)
    raise Exception("An unknown error occurred while compiling {}".format(protobuf_path))

def get_protos(bytes protobuf_path, list include_paths):
  cdef vector[string] c_include_paths = include_paths
  cdef vector[pair[string, string]] files
  cdef vector[cProtocError] errors
  # NOTE: Abbreviated name used to avoid shadowing of the module name.
  cdef vector[cProtocWarning] wrnings
  rc = protoc_get_protos(protobuf_path, &c_include_paths, &files, &errors, &wrnings)
  _handle_errors(rc, &errors, &wrnings, protobuf_path)
  return files

def get_services(bytes protobuf_path, list include_paths):
  cdef vector[string] c_include_paths = include_paths
  cdef vector[pair[string, string]] files
  cdef vector[cProtocError] errors
  # NOTE: Abbreviated name used to avoid shadowing of the module name.
  cdef vector[cProtocWarning] wrnings
  rc = protoc_get_services(protobuf_path, &c_include_paths, &files, &errors, &wrnings)
  _handle_errors(rc, &errors, &wrnings, protobuf_path)
  return files

