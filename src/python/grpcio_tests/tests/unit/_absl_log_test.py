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


class AbslLogTest(unittest.TestCase):

    def setUp(self):
        # Basic client script to start a grpc channel to a non-existent server,
        # just to check the GRPC_TRACE logs.
        script = "import grpc; channel = grpc.insecure_channel('localhost:1234'); print('Channel created'); channel.close()"

        # Set up the environment variables
        env = os.environ.copy()
        env["GRPC_TRACE"] = "api"

        # Run the subprocess and capture both stdout and stderr.
        process = subprocess.run(
            [sys.executable, "-c", script],
            check=False,
            env=env,
            capture_output=True,
            text=True,
            timeout=2,
        )

        self.output = process.stdout + process.stderr
        self.lines = self.output.strip().split("\n")

    def test_no_absl_log_warning(self):
        """Check that the absl warning message is not in the log"""

        absl_warning = (
            "WARNING: All log messages before absl::InitializeLog() is called"
            " are written to STDERR"
        )
        self.assertFalse(
            absl_warning in self.output,
            f"absl::InitializeLog() warning unexpectedly found in output:\n{self.output}",
        )

    def test_grpc_trace_log(self):
        """Checks that the expected grpc trace messages are in the log"""

        expected_api_traces = [
            "gRPC Tracers: api",
            "grpc_init(",
            "grpc_channel_create(",
            "grpc_channel_destroy(",
            "grpc_shutdown(",
        ]

        missing_traces = []
        for trace in expected_api_traces:
            if trace not in self.output:
                missing_traces.append(trace)

        self.assertTrue(
            not missing_traces,
            f"Missing expected api trace(s) {missing_traces}, in output:\n"
            f"{self.output}",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
