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

import glob
import os
import os.path
import shutil

import nox

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + "../../../../")
GRPC_ROOT = os.path.relpath(GRPC_STEM, start=GRPC_STEM)
PYTHON_REL_PATH = os.path.relpath(PYTHON_STEM, start=GRPC_STEM)


class CommandError(Exception):
    """Simple exception class for GRPC custom commands."""


@nox.session
def doc(session: nox.Session):
    """Session to generate documentation via sphinx."""

    # We import here to ensure that setup.py has had a chance to install the
    # relevant package eggs first.
    import sphinx.cmd.build

    session.log("Running doc gen via sphinx...")

    source_dir = os.path.join(GRPC_ROOT, "doc", "python", "sphinx")
    target_dir = os.path.join(GRPC_ROOT, "doc", "build")
    exit_code = sphinx.cmd.build.build_main(
        ["-b", "html", "-W", "--keep-going", source_dir, target_dir]
    )
    if exit_code != 0:
        raise CommandError("Documentation generation has warnings or errors")


@nox.session
def clean(session: nox.Session):
    """Session to clean build artifacts."""

    _FILE_PATTERNS = (
        "pyb",
        "src/python/grpcio/__pycache__/",
        "src/python/grpcio/grpc/_cython/cygrpc.cpp",
        "src/python/grpcio/grpc/_cython/*.so",
        "src/python/grpcio/grpcio.egg-info/",
    )
    _CURRENT_DIRECTORY = os.path.normpath(
        os.path.join(os.path.dirname(os.path.realpath(__file__)), "../../..")
    )

    for path_spec in _FILE_PATTERNS:
        this_glob = os.path.normpath(
            os.path.join(_CURRENT_DIRECTORY, path_spec)
        )
        abs_paths = glob.glob(this_glob)
        for path in abs_paths:
            if not str(path).startswith(_CURRENT_DIRECTORY):
                raise ValueError("Cowardly refusing to delete {}.".format(path))
            print("Removing {}".format(os.path.relpath(path)))
            if os.path.isfile(path):
                os.remove(str(path))
            else:
                shutil.rmtree(str(path))
