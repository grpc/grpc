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

from grpc_tools import protoc
import setuptools

if sys.version_info >= (3, 9, 0):
    from importlib import resources
else:
    import pkg_resources


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


def build_package_protos(package_root, strict_mode=False):
    proto_files = []
    inclusion_root = os.path.abspath(package_root)
    for root, _, files in os.walk(inclusion_root):
        for filename in files:
            if filename.endswith(".proto"):
                proto_files.append(
                    os.path.abspath(os.path.join(root, filename))
                )

    well_known_protos_include = _get_resource_file_name("grpc_tools", "_proto")

    for proto_file in proto_files:
        command = [
            "grpc_tools.protoc",
            "--proto_path={}".format(inclusion_root),
            "--proto_path={}".format(well_known_protos_include),
            "--python_out={}".format(inclusion_root),
            "--pyi_out={}".format(inclusion_root),
            "--grpc_python_out={}".format(inclusion_root),
        ] + [proto_file]
        if protoc.main(command) != 0:
            if strict_mode:
                raise Exception("error: {} failed".format(command))
            else:
                sys.stderr.write("warning: {} failed".format(command))


class BuildPackageProtos(setuptools.Command):
    """Command to generate project *_pb2.py modules from proto files."""

    description = "build grpc protobuf modules"
    user_options = [
        (
            "strict-mode",
            "s",
            "exit with non-zero value if the proto compiling fails.",
        )
    ]

    def initialize_options(self):
        self.strict_mode = False

    def finalize_options(self):
        pass

    def run(self):
        # due to limitations of the proto generator, we require that only *one*
        # directory is provided as an 'include' directory. We assume it's the '' key
        # to `self.distribution.package_dir` (and get a key error if it's not
        # there).
        build_package_protos(
            self.distribution.package_dir[""], self.strict_mode
        )
