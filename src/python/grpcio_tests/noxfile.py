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

import argparse
import os
import shutil

import nox

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + "../../../../")
PYTHON_REL_PATH = os.path.relpath(PYTHON_STEM, start=GRPC_STEM)
GRPC_PROTO_STEM = os.path.join("src", "proto")
PROTO_STEM = os.path.join(PYTHON_REL_PATH, "src", "proto")
PYTHON_PROTO_TOP_LEVEL = os.path.join(PYTHON_REL_PATH, "src")


@nox.session
def preprocess(session: nox.Session):
    """Session to gather proto dependencies"""

    # TODO(atash) ensure that we're running from the repository directory when
    # this command is used
    try:
        shutil.rmtree(PROTO_STEM)
    except Exception as error:
        # We don't care if this command fails
        pass
    shutil.copytree(GRPC_PROTO_STEM, PROTO_STEM)
    for root, _, _ in os.walk(PYTHON_PROTO_TOP_LEVEL):
        path = os.path.join(root, "__init__.py")
        open(path, "a").close()


@nox.session(venv_params=["--system-site-packages"])
def build_package_protos(session: nox.Session):
    """Command to generate project *_pb2.py modules from proto files."""
    session.log("Running build_package_protos for grpcio-tools...")

    # Note: We use exit_on_error=False to play nice with Nox
    parser = argparse.ArgumentParser(exit_on_error=False)

    parser.add_argument(
        "-s",
        "--strict-mode",
        action="store_true",
        help="exit with non-zero value if the proto compiling fails.",
    )

    # Parse the arguments from session.posargs
    # Any unknown args will be collected in 'unknown'
    args, unknown = parser.parse_known_args(session.posargs)

    # due to limitations of the proto generator, we require that only *one*
    # directory is provided as an 'include' directory.
    # We provide the value of `distribution.package_dir` set in pyproject.toml
    # which is the same directory as the pyproject.toml file
    from grpc_tools import command

    command.build_package_protos(PYTHON_STEM, args.strict_mode)
