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
    import importlib
    import contextlib

    _protos = protobuf.protos

    # TODO: Somehow access this function from protobuf?
    def _proto_file_to_module_name(suffix, proto_file):
        components = proto_file.split(os.path.sep)
        proto_base_name = os.path.splitext(components[-1])[0]
        return ".".join(components[:-1] + [proto_base_name + suffix])


    # TODO: Somehow access this function from protobuf?
    @contextlib.contextmanager
    def _augmented_syspath(new_paths):
        original_sys_path = sys.path
        if new_paths is not None:
            sys.path = sys.path + new_paths
        try:
            yield
        finally:
            sys.path = original_sys_path


    def _services(protobuf_path, include_paths=None):
        protobuf.protos(protobuf_path, include_paths)
        with _augmented_syspath(include_paths):
            module_name = _proto_file_to_module_name(_SERVICE_MODULE_SUFFIX,
                                                     protobuf_path)
            module = importlib.import_module(module_name)
            return module


    def _protos_and_services(protobuf_path, include_paths=None):
        return (protobuf.protos(protobuf_path, include_paths=include_paths),
                _services(protobuf_path, include_paths=include_paths))


    sys.meta_path.extend([
        protobuf.ProtoFinder(_SERVICE_MODULE_SUFFIX, _protoc_compiler.grpc_code_generator)
    ])


if __name__ == '__main__':
    proto_include = pkg_resources.resource_filename('grpc_tools', '_proto')
    sys.exit(main(sys.argv + ['-I{}'.format(proto_include)]))
