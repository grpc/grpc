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

import os
import pkg_resources
import sys

from grpc_tools import _protoc_compiler


def main(command_arguments, include_module_proto=True):
    """Run the protocol buffer compiler with the given command-line arguments.

  Args:
    command_arguments: A list of strings representing command line arguments to
        `protoc`.
    include_module_proto: Wether `*.proto` files comming with the current py-module
        will be included too.
  """
    if include_module_proto:
        proto_include = pkg_resources.resource_filename('grpc_tools', '_proto')
        command_arguments.append('-I{}'.format(proto_include))

    command_arguments = [os.path.abspath(__file__)] + command_arguments
    command_arguments = [argument.encode() for argument in command_arguments]
    return _protoc_compiler.run_main(command_arguments)


if __name__ == '__main__':
    retcode = main(sys.argv[1:], include_module_proto=True)
    sys.exit(retcode)
