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
"""Provides setuptools command classes for the gRPC Python setup process."""

import glob
import os
import os.path
import platform
import re
import shutil
import sys

import setuptools
from setuptools import errors as _errors
from setuptools.command import build_ext
from setuptools.command import build_py
from setuptools.command import easy_install
from setuptools.command import install
from setuptools.command import test

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + "../../../../")
PYTHON_REL_PATH = os.path.relpath(PYTHON_STEM, start=GRPC_STEM)
GRPC_PROTO_STEM = os.path.join("src", "proto")
PROTO_STEM = os.path.join(PYTHON_REL_PATH, "src", "proto")
PYTHON_PROTO_TOP_LEVEL = os.path.join(PYTHON_REL_PATH, "src")


class RunFork(setuptools.Command):
    description = "run fork test client"
    user_options = [("args=", "a", "pass-thru arguments for the client")]

    def initialize_options(self):
        self.args = ""

    def finalize_options(self):
        # distutils requires this override.
        pass

    def run(self):
        # We import here to ensure that our setuptools parent has had a chance to
        # edit the Python system path.
        from tests.fork import client

        sys.argv[1:] = self.args.split()
        client.test_fork()
