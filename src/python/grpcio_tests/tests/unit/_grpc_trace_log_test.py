# Copyright 2026 gRPC authors.
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
"""Tests if GRPC_TRACE logging is working."""

import os
import subprocess
import sys
import unittest


class TraceLogTest(unittest.TestCase):

    def test_grpc_trace_log(self):
        # Basic client script to start a grpc channel to a non-existent server,
        # just to check the GRPC_TRACE logs.
        script = "import grpc; channel = grpc.insecure_channel('localhost:1234'); channel.close()"

        # Set up the environment
        env = os.environ.copy()
        env["GRPC_TRACE"] = "api"

        # Run the subprocess and capture both stdout and stderr.
        process = subprocess.run(
            [sys.executable, "-c", script],
            check=False,
            env=env,
            capture_output=True,
            text=True,
        )

        output = process.stdout + process.stderr
        lines = output.strip().split("\n")

        expected_min_log_lines = 13

        # Log the output for debugging if the test fails
        if len(lines) <= expected_min_log_lines:
            print(f"Subprocess output (total {len(lines)} lines):")
            print(output)

        self.assertGreaterEqual(len(lines), expected_min_log_lines)

        # Check for the absl warning message in the first 5 lines
        expected_warning = "WARNING: All log messages before absl::InitializeLog() is called are written to STDERR"
        found_warning = any(expected_warning in line for line in lines[:5])
        self.assertFalse(found_warning)


if __name__ == "__main__":
    unittest.main(verbosity=2)
