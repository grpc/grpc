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
"""Deadlock debuggers shared by the concurrency test suites.

Debugging a deadlock (two options):
  * GRPC_FT_GDB_DUMP_AFTER_SECONDS=<n> - a watchdog spawns gdb against this very
    process after n seconds and dumps every thread's Python stack to the
    stderr, then aborts. Needs gdb on PATH; point GRPC_FT_LIBPYTHON_GDB at
    CPython's Tools/gdb/libpython.py for `py-bt` frames
  * GRPC_FT_DEBUG_HANG=1 - print FT_TEST_PID=<pid>, for attaching gdb manually
    while test stays hung

Debugging suspended coroutines:
  * GRPC_FT_AIO_TASK_DUMP_AFTER_SECONDS=<n> a watchdog dumps every pending's
    task;s await stack, then aborts
"""

import asyncio
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import threading

_DEFAULT_LIBPYTHON_GDB = str(
    Path.home() / "projects/cpython-3.14.6-tsan/Tools/gdb/libpython.py"
)
# below can be found in `cat /usr/include/linux/prctl.h | grep "PTRACER"`
_PR_SET_PTRACER = 0x59616D61
_PR_SET_PTRACER_ANY = ctypes.c_ulong(-1)


def _start_gdb_watchdog(seconds, register_cleanup):
    """Dump all threads via gdb if the test hasn't finished in `seconds`"""
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        # this is needed to call `gdb -p <pid>` without `sudo`
        libc.prctl(_PR_SET_PTRACER, _PR_SET_PTRACER_ANY, 0, 0, 0)
    except OSError:
        pass

    pid = os.getpid()

    libpython = os.environ.get("GRPC_FT_LIBPYTHON_GDB", _DEFAULT_LIBPYTHON_GDB)
    assert(os.path.exists(libpython))

    done = threading.Event()
    register_cleanup(done.set)

    def watchdog():
        if done.wait(seconds):
            return  # test finished; nothing hung
        sys.stderr.write(
            f"\n=== GRPC_FT: no progress after {seconds}s; gdb-dumping pid:{pid} ===\n"
        )
        sys.stderr.flush()
        cmd = [
            "gdb", "-batch", "-p", str(pid),
            "-ex", "set pagination off",
            "-ex", "source " + libpython,
            "-ex", "thread apply all py-bt"]
        # stripping PYTHONPATH / PYTHONHOME / PYTHON_GIL from gdb;
        # its embedded Python will start with a clean path
        env = {
            k: v for k, v in os.environ.items() if not k.startswith("PYTHON")
        }
        try:
            subprocess.run(cmd, stdout=2, stderr=2, env=env, timeout=180)
        except Exception as ex:  # pylint: disable=broad-except
            sys.stderr.write(f"gdb-dump failed: {ex}\n")
            sys.stderr.flush()
        os.abort()

    threading.Thread(target=watchdog, daemon=True, name="gdb-watchdog").start()


def install_deadlock_debuggers(register_cleanup):
    if os.environ.get("GRPC_FT_DEBUG_HANG"):
        sys.stderr.write(f"FT_TEST_PID={os.getpid()}\n")
        sys.stderr.flush()

    gdb_dump_after = os.environ.get("GRPC_FT_GDB_DUMP_AFTER_SECONDS")
    if gdb_dump_after:
        _start_gdb_watchdog(float(gdb_dump_after), register_cleanup)


def _start_task_watchdog(seconds, register_cleanup):
    """Dump all asyncio pending tasks await stack."""
    loop = asyncio.get_running_loop()

    done = threading.Event()
    register_cleanup(done.set)

    def watchdog():
        if done.wait(seconds):
            return  # test finished; nothing hung
        sys.stderr.write(
            f"\n=== GRPC_FT: no progress after {seconds}s; dumping asyncio tasks ===\n"
        )
        for task in asyncio.all_tasks(loop):
            task.print_stack(file=sys.stderr)
        sys.stderr.flush()
        os.abort()

    threading.Thread(
        target=watchdog, daemon=True, name="asyncio_task_watchdog"
    ).start()


def install_asyncio_suspended_task_debuggers(register_cleanup):
    task_dump_after = os.environ.get("GRPC_FT_AIO_TASK_DUMP_AFTER_SECONDS")
    if task_dump_after:
        _start_task_watchdog(float(task_dump_after), register_cleanup)
