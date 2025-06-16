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
import os
import platform
import subprocess
import sys

import tempfile

_GDB_TIMEOUT_S = 60


def print_backtraces(pid):
    # Check if we are on macOS AND the environment variable is set
    use_lldb = False
    if platform.system() == "Darwin":
        use_lldb = bool(os.environ.get("USE_LLDB_ON_DARWIN"))
        if not use_lldb:
            sys.stderr.write(
                "LLDB is recommended for use on Mac.\n"
                + 'Run "sudo DevToolsSecurity -enable" and set USE_LLDB_ON_DARWIN'
                + " environment variable to use lldb\n"
            )
    # Determine the OS and set debugger specific commands
    if use_lldb:  # macOS
        debugger_name = "lldb"
        cmd = [
            "lldb",
            "-b",  # Batch mode: exit after commands are executed
            "-o",
            "process attach --pid {}".format(pid),
            "-o",
            "thread backtrace all",  # Dumps backtraces of all threads
            "-o",
            "quit",
        ]
    else:  # Assume Linux/Unix-like for gdb (adjust if other OSes need specific handling)
        debugger_name = "gdb"
        cmd = [
            "gdb",
            "-ex",
            "set confirm off",
            "-ex",
            "attach {}".format(pid),
            "-ex",
            "thread apply all bt",
            "-ex",
            "quit",
        ]
    streams = tuple(tempfile.TemporaryFile() for _ in range(2))
    sys.stderr.write("Invoking gdb\n")
    sys.stderr.flush()
    process = subprocess.Popen(cmd, stdout=streams[0], stderr=streams[1])
    try:
        process.wait(timeout=_GDB_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        sys.stderr.write("{} stacktrace generation timed out.\n".format(debugger_name))
    finally:
        for stream_name, stream in zip(("STDOUT", "STDERR"), streams):
            stream.seek(0)
            sys.stderr.write(
                "{} {}:\n{}\n".format(
                    debugger_name, stream_name, stream.read().decode("ascii")
                )
            )
            stream.close()
        sys.stderr.flush()
