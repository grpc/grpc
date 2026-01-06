# Copyright 2015 gRPC authors.
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
"""Provides setuptools command classes for the GRPC Python setup process."""

import logging
import os
import pathlib
import shutil

import setuptools

ROOT_DIR = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))
GRPC_ROOT_ABS_PATH = os.path.join(ROOT_DIR, "../../..")
ROOT_REL_DIR = os.path.relpath(ROOT_DIR, start=GRPC_ROOT_ABS_PATH)
HEALTH_PROTO = "src/proto/grpc/health/v1/health.proto"
LICENSE = "./LICENSE"


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
        # TODO: Can skip copy proto part.
        health_proto_dst = os.path.join(
            ROOT_REL_DIR, "grpc_health/v1/health.proto"
        )
        if os.path.isfile(HEALTH_PROTO):
            shutil.copyfile(HEALTH_PROTO, health_proto_dst)
        else:
            self.announce(
                f"Copy '{HEALTH_PROTO}' -> '{health_proto_dst}' failed: file not found",
                level=logging.WARNING,
            )

        license_dst = os.path.join(ROOT_REL_DIR, "LICENSE")
        if os.path.isfile(LICENSE):
            shutil.copyfile(LICENSE, license_dst)
        else:
            self.announce(
                f"Copy '{LICENSE}' -> '{license_dst}' failed: file not found",
                level=logging.WARNING,
            )


class BuildPackageProtos(setuptools.Command):
    """Command to generate project *_pb2.py modules from proto files."""

    description = "build grpc protobuf modules"
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        from grpc_tools import command

        protos_search_dir = pathlib.Path(__file__).parent / "grpc_health"

        protos = protos_search_dir.glob("**/*.proto")
        if not next(protos, None):
            raise RuntimeError("No protos found")

        # find and build all protos in the current package
        command.build_package_protos(protos_search_dir)
