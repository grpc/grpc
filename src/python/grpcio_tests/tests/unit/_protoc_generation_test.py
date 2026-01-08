# Copyright 2026 The gRPC Authors
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
"""Test of protoc generation via python -m grpcio_tools.protoc."""

import logging
import os
import subprocess
import sys
import tempfile
import unittest

_DATA_DIR = os.path.join(os.path.dirname(__file__), "data", "service")
_PROTO_FILE = os.path.join(_DATA_DIR, "simple.proto")

class ProtocGenerationTest(unittest.TestCase):

    def test_protoc_generation(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            # Run python -m grpcio_tools.protoc
            cmd = [
                sys.executable,
                "-m",
                "grpc_tools.protoc",
                "-I{}".format(_DATA_DIR),
                "--python_out={}".format(tmp_dir),
                "--grpc_python_out={}".format(tmp_dir),
                os.path.basename(_PROTO_FILE),
            ]
            
            env = os.environ.copy()
            if "PYTHONPATH" not in env:
                 env["PYTHONPATH"] = os.pathsep.join(sys.path)

            subprocess.check_call(cmd, env=env)

            # Add tmp_dir to sys.path to import generated modules
            sys.path.insert(0, tmp_dir)
            try:
                import simple_pb2
                import simple_pb2_grpc
                
                self.assertTrue(hasattr(simple_pb2, "SimpleRequest"))
                self.assertTrue(hasattr(simple_pb2_grpc, "SimpleServiceStub"))
            finally:
                sys.path.pop(0)

if __name__ == "__main__":
    logging.basicConfig()
    unittest.main()
