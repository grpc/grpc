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

import nox

ROOT_DIR = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))


@nox.session
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
    from . import command

    command.build_package_protos(os.path.join(ROOT_DIR, ".."), args.strict_mode)
