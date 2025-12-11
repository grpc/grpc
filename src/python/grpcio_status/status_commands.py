# Copyright 2018 The gRPC Authors
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

import os
import shutil

import setuptools

ROOT_DIR = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))
STATUS_PROTO = os.path.join(
    ROOT_DIR, "../../../third_party/googleapis/google/rpc/status.proto"
)
PACKAGE_STATUS_PROTO_PATH = "grpc_status/google/rpc"
LICENSE = os.path.join(ROOT_DIR, "../../../LICENSE")


class Preprocess(setuptools.Command):
    """Command to copy LICENSE from root directory."""

    description = ""
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        if os.path.isfile(STATUS_PROTO):
            if not os.path.isdir(PACKAGE_STATUS_PROTO_PATH):
                os.makedirs(PACKAGE_STATUS_PROTO_PATH)
            shutil.copyfile(
                STATUS_PROTO,
                os.path.join(
                    ROOT_DIR, PACKAGE_STATUS_PROTO_PATH, "status.proto"
                ),
            )
        if os.path.isfile(LICENSE):
            shutil.copyfile(LICENSE, os.path.join(ROOT_DIR, "LICENSE"))
