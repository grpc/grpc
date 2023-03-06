# Copyright 2023 gRPC authors.
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

# Cross-platform test runner for gRPC end2end tests

import os
import subprocess
import sys

from rules_python.python.runfiles import runfiles


def UsageString() -> str:
    return "Usage: run.py test_fixture [test ...]"


def main():
    assert len(sys.argv) >= 2, UsageString()
    test_fixture = sys.argv[1]
    r = runfiles.Create()
    # even bazel on Windows appears to use the '/' delimiter
    path = "/".join([os.environ["TEST_WORKSPACE"], test_fixture])
    executable_name = r.Rlocation(path)
    cmds = [executable_name]
    if len(sys.argv) > 2:
        cmds.extend(sys.argv[2:])
    subprocess.run(cmds, check=True)


if __name__ == "__main__":
    main()
