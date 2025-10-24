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
LICENSE = "./LICENSE"


@nox.session
def preprocess(session: nox.Session):
    """
    Session to copy the root LICENSE and status.proto files into the package source.
    """
    session.log("Running preprocess for grpcio-testing...")
    session.log("Current working directory:", os.path.abspath(os.curdir))
    session.log(
        "Current working directory contents:",
        os.listdir(os.path.abspath(os.curdir)),
    )
    session.log(
        "ROOT_REL_DIR contents:", os.listdir(os.path.abspath(ROOT_REL_DIR))
    )
    if os.path.isfile(LICENSE):
        shutil.copyfile(LICENSE, os.path.join(ROOT_REL_DIR, "LICENSE"))
