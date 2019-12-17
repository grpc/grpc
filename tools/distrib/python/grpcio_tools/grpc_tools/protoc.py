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

import os

from grpc_tools import _protoc_compiler

_SERVICE_MODULE_SUFFIX = "_pb2_grpc"


def main(command_arguments):
    """Run the protocol buffer compiler with the given command-line arguments.

  Args:
    command_arguments: a list of strings representing command line arguments to
        `protoc`.
  """
    command_arguments = [argument.encode() for argument in command_arguments]
    return _protoc_compiler.run_main(command_arguments)


if sys.version_info[0] > 2:
    from google import protobuf

    _protos = protobuf.protos

    finder, _import_raw = protobuf.get_import_machinery(_SERVICE_MODULE_SUFFIX,
                                                        _protoc_compiler.grpc_code_generator)

    def _services(protobuf_path, include_paths=None):
        protobuf.protos(protobuf_path, include_paths=include_paths)
        return _import_raw(protobuf_path, include_paths=include_paths)

    def _protos_and_services(protobuf_path, include_paths=None):
        return (protobuf.protos(protobuf_path, include_paths=include_paths),
                _services(protobuf_path, include_paths=include_paths))


    sys.meta_path.extend([finder])


if __name__ == '__main__':
    proto_include = pkg_resources.resource_filename('grpc_tools', '_proto')
    sys.exit(main(sys.argv + ['-I{}'.format(proto_include)]))
