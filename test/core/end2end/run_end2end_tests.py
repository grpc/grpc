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
from pathlib import Path
import subprocess
import sys

from rules_python.python.runfiles import runfiles
from typing import Tuple


def GetBinaryAbsolutePath(fixture_name: str) -> str:
    """Finds the absolute path to the test binary using bazel's runfile tools.

    On Windows, there are no symlinks in the runfiles directory like there are
    on Posix systems. This tool solves the problem of locating the test
    binaries on any platform.
    """
    r = runfiles.Create()
    # bazel's Rlocation search expects the '/' delimiter, even on Windows
    fixture_path = "/".join([os.environ["TEST_WORKSPACE"], fixture_name])
    return r.Rlocation(str(fixture_path))


def SplitBinaryPathByRunfileLocation(abspath: str) -> Tuple[str, str]:
    """Converts the path to platform-specific cwd and related path strings."""
    exec_cwd, exec_path = None, abspath
    if sys.platform == "win32":
        # Minimize the path length. This is not done for Linux/Mac because it
        # affects the *SAN suppressions list search.
        path = Path(abspath)
        exec_cwd = path.parent
        exec_path = path.name
    return exec_cwd, exec_path


def main() -> None:
    assert len(sys.argv) >= 2, "Usage: run.py test_fixture [test ...]"
    executable_name = GetBinaryAbsolutePath(sys.argv[1])
    # To avoid long filenames on Windows, split the executable name into a
    # reasonable cwd and path.
    exec_cwd, exec_path = SplitBinaryPathByRunfileLocation(executable_name)
    cmds = [exec_path]
    if len(sys.argv) > 2:
        cmds.extend(sys.argv[2:])
    print(f"Executing {cmds} in cwd={exec_cwd}")
    # On Windows, shell=True is necessary for `cwd` to be used for the binary
    # search path.
    subprocess.run(' '.join(cmds), check=True, cwd=exec_cwd, shell=True)


if __name__ == "__main__":
    main()
