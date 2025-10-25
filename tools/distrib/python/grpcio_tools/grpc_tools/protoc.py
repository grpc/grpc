#!/usr/bin/env python3

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
import sys

from grpc_tools import _protoc_compiler

if sys.version_info >= (3, 9, 0):
    from importlib import resources
else:
    import pkg_resources

_PROTO_MODULE_SUFFIX = "_pb2"
_SERVICE_MODULE_SUFFIX = "_pb2_grpc"

_DISABLE_DYNAMIC_STUBS = "GRPC_PYTHON_DISABLE_DYNAMIC_STUBS"


def main(command_arguments):
    """Run the protocol buffer compiler with the given command-line arguments.

    Args:
      command_arguments: a list of strings representing command line arguments to
          `protoc`.
    """
    command_arguments = [argument.encode() for argument in command_arguments]
    return _protoc_compiler.run_main(command_arguments)


def _get_resource_file_name(
    package_or_requirement: str, resource_name: str
) -> str:
    """Obtain the filename for a resource on the file system."""
    file_name = None
    if sys.version_info >= (3, 9, 0):
        file_name = (
            resources.files(package_or_requirement) / resource_name
        ).resolve()
    else:
        file_name = pkg_resources.resource_filename(
            package_or_requirement, resource_name
        )
    return str(file_name)


# NOTE(rbellevi): importlib.abc is not supported on 3.4.
if sys.version_info >= (3, 5, 0):
    import contextlib
    import importlib
    import importlib.abc
    import importlib.machinery
    import threading

    _FINDERS_INSTALLED = False
    _FINDERS_INSTALLED_LOCK = threading.Lock()

    def _maybe_install_proto_finders():
        global _FINDERS_INSTALLED
        with _FINDERS_INSTALLED_LOCK:
            if not _FINDERS_INSTALLED:
                sys.meta_path.extend(
                    [
                        ProtoFinder(
                            _PROTO_MODULE_SUFFIX, _protoc_compiler.get_protos
                        ),
                        ProtoFinder(
                            _SERVICE_MODULE_SUFFIX,
                            _protoc_compiler.get_services,
                        ),
                    ]
                )

                proto_include = _get_resource_file_name("grpc_tools", "_proto")
                sys.path.append(proto_include)

                _FINDERS_INSTALLED = True

    def _module_name_to_proto_file(suffix, module_name):
        components = module_name.split(".")
        proto_name = components[-1][: -1 * len(suffix)]
        # NOTE(rbellevi): The Protobuf library expects this path to use
        # forward slashes on every platform.
        return "/".join(components[:-1] + [proto_name + ".proto"])

    def _proto_file_to_module_name(suffix, proto_file):
        components = proto_file.split(os.path.sep)
        proto_base_name = os.path.splitext(components[-1])[0]
        return ".".join(components[:-1] + [proto_base_name + suffix])

    def _protos(protobuf_path):
        """Returns a gRPC module generated from the indicated proto file."""
        _maybe_install_proto_finders()
        module_name = _proto_file_to_module_name(
            _PROTO_MODULE_SUFFIX, protobuf_path
        )
        module = importlib.import_module(module_name)
        return module

    def _services(protobuf_path):
        """Returns a module generated from the indicated proto file."""
        _maybe_install_proto_finders()
        _protos(protobuf_path)
        module_name = _proto_file_to_module_name(
            _SERVICE_MODULE_SUFFIX, protobuf_path
        )
        module = importlib.import_module(module_name)
        return module

    def _protos_and_services(protobuf_path):
        """Returns two modules, corresponding to _pb2.py and _pb2_grpc.py files."""
        return (_protos(protobuf_path), _services(protobuf_path))

    _proto_code_cache = {}
    _proto_code_cache_lock = threading.RLock()

    class ProtoLoader(importlib.abc.Loader):
        def __init__(
            self, suffix, codegen_fn, module_name, protobuf_path, proto_root
        ):
            self._suffix = suffix
            self._codegen_fn = codegen_fn
            self._module_name = module_name
            self._protobuf_path = protobuf_path
            self._proto_root = proto_root

        def create_module(self, spec):
            return None

        def _generated_file_to_module_name(self, filepath):
            components = filepath.split(os.path.sep)
            return ".".join(
                components[:-1] + [os.path.splitext(components[-1])[0]]
            )

        def exec_module(self, module):
            assert module.__name__ == self._module_name
            code = None
            with _proto_code_cache_lock:
                if self._module_name in _proto_code_cache:
                    code = _proto_code_cache[self._module_name]
                    exec(code, module.__dict__)
                else:
                    files = self._codegen_fn(
                        self._protobuf_path.encode("ascii"),
                        [path.encode("ascii") for path in sys.path],
                    )
                    # NOTE: The files are returned in topological order of dependencies. Each
                    # entry is guaranteed to depend only on the modules preceding it in the
                    # list and the last entry is guaranteed to be our requested module. We
                    # cache the code from the first invocation at module-scope so that we
                    # don't have to regenerate code that has already been generated by protoc.
                    for f in files[:-1]:
                        module_name = self._generated_file_to_module_name(
                            f[0].decode("ascii")
                        )
                        if module_name not in sys.modules:
                            if module_name not in _proto_code_cache:
                                _proto_code_cache[module_name] = f[1]
                            importlib.import_module(module_name)
                    exec(files[-1][1], module.__dict__)

    class ProtoFinder(importlib.abc.MetaPathFinder):
        def __init__(self, suffix, codegen_fn):
            self._suffix = suffix
            self._codegen_fn = codegen_fn

        def find_spec(self, fullname, path, target=None):
            if not fullname.endswith(self._suffix):
                return None
            filepath = _module_name_to_proto_file(self._suffix, fullname)
            for search_path in sys.path:
                try:
                    prospective_path = os.path.join(search_path, filepath)
                    os.stat(prospective_path)
                except (FileNotFoundError, NotADirectoryError, OSError):
                    continue
                else:
                    return importlib.machinery.ModuleSpec(
                        fullname,
                        ProtoLoader(
                            self._suffix,
                            self._codegen_fn,
                            fullname,
                            filepath,
                            search_path,
                        ),
                    )

    # NOTE(rbellevi): We provide an environment variable that enables users to completely
    # disable this behavior if it is not desired, e.g. for performance reasons.
    if not os.getenv(_DISABLE_DYNAMIC_STUBS):
        _maybe_install_proto_finders()


def entrypoint() -> None:
    proto_include = _get_resource_file_name("grpc_tools", "_proto")
    sys.exit(main(sys.argv + ["-I{}".format(proto_include)]))


if __name__ == "__main__":
    entrypoint()
