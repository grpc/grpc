# Copyright 2019 the gRPC authors.
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
"""Test for multiprocessing example."""

import ast
import logging
import math
import os
import re
import subprocess
import tempfile
import unittest

_BINARY_DIR = os.path.realpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
)
_SERVER_PATH = os.path.join(_BINARY_DIR, "server")
_CLIENT_PATH = os.path.join(_BINARY_DIR, "client")


def is_prime(n):
    for i in range(2, int(math.ceil(math.sqrt(n)))):
        if n % i == 0:
            return False
    else:
        return True


def _get_server_address(server_stream):
    while True:
        server_stream.seek(0)
        line = server_stream.readline()
        while line:
            matches = re.search("Binding to '(.+)'", line)
            if matches is not None:
                return matches.groups()[0]
            line = server_stream.readline()


class MultiprocessingExampleTest(unittest.TestCase):
    def test_multiprocessing_example(self):
        server_stdout = tempfile.TemporaryFile(mode="r")
        server_process = subprocess.Popen((_SERVER_PATH,), stdout=server_stdout)
        server_address = _get_server_address(server_stdout)
        client_stdout = tempfile.TemporaryFile(mode="r")
        client_process = subprocess.Popen(
            (
                _CLIENT_PATH,
                server_address,
            ),
            stdout=client_stdout,
        )
        client_process.wait()
        server_process.terminate()
        client_stdout.seek(0)
        results = ast.literal_eval(client_stdout.read().strip().split("\n")[-1])
        values = tuple(result[0] for result in results)
        self.assertSequenceEqual(range(2, 10000), values)
        for result in results:
            self.assertEqual(is_prime(result[0]), result[1])


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
