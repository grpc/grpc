# Copyright 2025 The gRPC Authors
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
"""Provides nox command classes for the GRPC Python setup process."""

import os
import shutil

import nox

ROOT_DIR = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))
GRPC_ROOT_ABS_PATH = os.path.join(ROOT_DIR, "../../..")
ROOT_REL_DIR = os.path.relpath(ROOT_DIR, start=GRPC_ROOT_ABS_PATH)
REFLECTION_PROTO = "src/proto/grpc/reflection/v1alpha/reflection.proto"
LICENSE = "./LICENSE"


@nox.session
def preprocess(session: nox.Session):
    """
    Session to copy proto modules from grpc/src/proto and LICENSE from
    the root directory
    """
    session.log("Running preprocess for grpcio-reflection...")
    # TODO: Can skip copy proto part.
    if os.path.isfile(REFLECTION_PROTO):
        shutil.copyfile(
            REFLECTION_PROTO,
            os.path.join(
                ROOT_REL_DIR, "grpc_reflection/v1alpha/reflection.proto"
            ),
        )
    if os.path.isfile(LICENSE):
        shutil.copyfile(LICENSE, os.path.join(ROOT_REL_DIR, "LICENSE"))


# use this flag to use the pre-installed grpc_tools in the environment
@nox.session(venv_params=["--system-site-packages"])
def build_package_protos(session: nox.Session):
    """Command to generate project *_pb2.py modules from proto files."""
    session.log("Running build_package_protos for grpcio-reflection...")
    # due to limitations of the proto generator, we require that only *one*
    # directory is provided as an 'include' directory. We assume it's the '' key
    # to `self.distribution.package_dir` (and get a key error if it's not
    # there).
    from grpc_tools import command

    command.build_package_protos(ROOT_DIR)
