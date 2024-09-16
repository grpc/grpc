# Copyright 2024 gRPC authors.
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

import os
import re
import subprocess
import sys
import tempfile

_OK_TEST_REGEX = r"^-+.*Ran ([\d]+) tests* in ([\d.]+)s.*OK(?: \(skipped=(\d+)\))?\n$"

# Tests with known exception logs.
# TODO(sourabhsinghs): Investigate and enable _rpc_part_1_test and _rpc_part_2_test tests.
_SKIP_TESTS = [
    "_rpc_part_1_test",
    "_server_shutdown_test",
    "_xds_credentials_test",
    "_server_test",
    "_invalid_metadata_test",
    "_reconnect_test",
    "_channel_close_test",
    "_rpc_part_2_test",
    "_invocation_defects_test",
    "_dynamic_stubs_test",
    "_channel_connectivity_test",
]

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"USAGE: {sys.argv[0]} TARGET_MODULE", file=sys.stderr)
        sys.exit(1)

    test_script = sys.argv[1]
    target_module = sys.argv[2]

    if target_module in _SKIP_TESTS:
        print(f"Skipping {target_module}")
        sys.exit(0)

    command = [
        sys.executable,
        os.path.realpath(test_script),
        target_module,
        os.path.dirname(os.path.relpath(__file__)),
    ]

    with tempfile.TemporaryFile(mode="w+") as stdout_file:
        with tempfile.TemporaryFile(mode="w+") as stderr_file:
            result = subprocess.run(
                command,
                stdout=stdout_file,
                stderr=stderr_file,
                text=True,
                check=True,
            )

            stdout_file.seek(0)
            stderr_file.seek(0)

            stdout_count = len(stdout_file.readlines())
            stderr_count = len(stderr_file.readlines())

            if result.returncode != 0:
                sys.exit("Test failure")

            stderr_file.seek(0)
            if not re.fullmatch(_OK_TEST_REGEX, stderr_file.read(), re.DOTALL):
                print(
                    f"Warning: Excessive error output detected ({stderr_count} lines):"
                )
                stderr_file.seek(0)
                for line in stderr_file:
                    print(line)

            if stdout_count > 0:
                print(
                    f"Warning: Unexpected output detected ({stdout_count} lines):"
                )
                stdout_file.seek(0)
                for line in stdout_file:
                    print(line)

