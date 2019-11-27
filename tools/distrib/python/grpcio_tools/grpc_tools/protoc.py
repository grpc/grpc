#!/usr/bin/env python

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

import pkg_resources
import sys

# TODO: Figure out how to add this dependency to setuptools.
import six
import imp
import os

from grpc_tools import _protoc_compiler

def main(command_arguments):
    """Run the protocol buffer compiler with the given command-line arguments.

  Args:
    command_arguments: a list of strings representing command line arguments to
        `protoc`.
  """
    command_arguments = [argument.encode() for argument in command_arguments]
    return _protoc_compiler.run_main(command_arguments)

def _import_modules_from_files(files):
  modules = []
  for filename, code in files:
    base_name = os.path.basename(filename.decode('ascii'))
    proto_name, _ = os.path.splitext(base_name)
    anchor_package = ".".join(os.path.normpath(os.path.dirname(filename.decode('ascii'))).split(os.sep))
    module_name = "{}.{}".format(anchor_package, proto_name)
    if module_name not in sys.modules:
      # TODO: The imp module is apparently deprecated. Figure out how to migrate
      # over.
      module = imp.new_module(module_name)
      six.exec_(code, module.__dict__)
      sys.modules[module_name] = module
      modules.append(module)
    else:
      modules.append(sys.modules[module_name])
  return tuple(modules)

def get_protos(protobuf_path, include_path):
  files = _protoc_compiler.get_protos(protobuf_path.encode('ascii'), include_path.encode('ascii'))
  return _import_modules_from_files(files)[-1]

def get_services(protobuf_path, include_path):
  files = _protoc_compiler.get_services(protobuf_path.encode('ascii'), include_path.encode('ascii'))
  return _import_modules_from_files(files)[-1]


if __name__ == '__main__':
    proto_include = pkg_resources.resource_filename('grpc_tools', '_proto')
    sys.exit(main(sys.argv + ['-I{}'.format(proto_include)]))
