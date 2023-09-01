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
"""Provides distutils command classes for the GRPC Python setup process."""

import os
import shutil

import setuptools

ROOT_DIR = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))
REFLECTION_PROTO = os.path.join(
    ROOT_DIR, "../../proto/grpc/reflection/v1alpha/reflection.proto"
)
LICENSE = os.path.join(ROOT_DIR, "../../../LICENSE")


class Preprocess(setuptools.Command):
    """Command to copy proto modules from grpc/src/proto and LICENSE from
    the root directory"""

    description = ""
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        if os.path.isfile(REFLECTION_PROTO):
            shutil.copyfile(
                REFLECTION_PROTO,
                os.path.join(
                    ROOT_DIR, "grpc_reflection/v1alpha/reflection.proto"
                ),
            )
        if os.path.isfile(LICENSE):
            shutil.copyfile(LICENSE, os.path.join(ROOT_DIR, "LICENSE"))


class BuildPackageProtos(setuptools.Command):
    """Command to generate project *_pb2.py modules from proto files."""

    description = "build grpc protobuf modules"
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        # due to limitations of the proto generator, we require that only *one*
        # directory is provided as an 'include' directory. We assume it's the '' key
        # to `self.distribution.package_dir` (and get a key error if it's not
        # there).
        from grpc_tools import command

        command.build_package_protos(self.distribution.package_dir[""])
