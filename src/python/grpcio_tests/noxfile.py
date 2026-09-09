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
import platform
import re
import shutil
import sys

import nox
# PYTHON_STEM - # usr/local/google/home/janiewicz/Code/grpc/src/python/grpcio_tests
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
# GRPC_STEM - usr/local/google/home/janiewicz/Code/grpc
GRPC_ROOT_ABS_PATH = os.path.join(ROOT_DIR,"../../../")
# PYTHON_REL_PATH - src/python/grpcio_tests
ROOT_REL_DIR = os.path.relpath(ROOT_DIR, start=GRPC_ROOT_ABS_PATH)
GRPC_PROTO_STEM = os.path.join("src", "proto") # src/proto
PROTO_STEM = os.path.join(ROOT_REL_DIR, "src", "proto") # src/python/grpcio_tests/src/proto
PYTHON_PROTO_TOP_LEVEL = os.path.join(ROOT_REL_DIR, "src") # src/python/grpcio_tests/src

@nox.session
def preprocess(session: nox.Session):
    """
    Session to gather proto dependencies
    """
    session.log("Running preprocess for grpcio_tests...")

    session.cd(GRPC_ROOT_ABS_PATH)

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
  """
  Session to generate project *_pb2.py modules from proto files.
  """
  session.log("Running build_package_protos for grpcio_tests")

  from grpc_tools import command

  command.build_package_protos(ROOT_DIR)

@nox.session(venv_params=["--system-site-packages"])
def test_lite(session: nox.Session):
  """
  Session to run tests without fetching or building anything.
  """
  session.log("Running test_lite for grpcio_tests")

  session.cd(ROOT_DIR)
  if ROOT_DIR not in sys.path:
      sys.path.insert(0, ROOT_DIR)

  import tests

  loader = tests.Loader()
  loader.loadTestsFromNames(["tests"])
  runner = tests.Runner(dedicated_threads=True)
  result = runner.run(loader.suite)
  if not result.wasSuccessful():
      sys.exit("Test failure")
