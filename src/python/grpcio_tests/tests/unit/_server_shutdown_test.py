# Copyright 2018 gRPC authors.
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
"""Tests clean shutdown of server on various interpreter exit conditions.

The tests in this module spawn a subprocess for each test case, the
test is considered successful if it doesn't freeze/timeout.
"""

import atexit
import os
import subprocess
import sys
import threading
import unittest
import logging

from tests.unit import _server_shutdown_scenarios

SCENARIO_FILE = os.path.abspath(
    os.path.join(os.path.dirname(os.path.realpath(__file__)),
                 '_server_shutdown_scenarios.py'))
INTERPRETER = sys.executable
BASE_COMMAND = [INTERPRETER, SCENARIO_FILE]

processes = []
process_lock = threading.Lock()


# Make sure we attempt to clean up any
# processes we may have left running
def cleanup_processes():
    with process_lock:
        for process in processes:
            try:
                process.kill()
            except Exception:  # pylint: disable=broad-except
                pass


atexit.register(cleanup_processes)


def wait(process):
    with process_lock:
        processes.append(process)
    process.wait()


class ServerShutdown(unittest.TestCase):

    # Currently we shut down a server (if possible) after the Python server
    # instance is garbage collected. This behavior may change in the future.
    def test_deallocated_server_stops(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_server_shutdown_scenarios.SERVER_DEALLOCATED],
            stdout=sys.stdout,
            stderr=sys.stderr)
        wait(process)

    def test_server_exception_exits(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_server_shutdown_scenarios.SERVER_RAISES_EXCEPTION],
            stdout=sys.stdout,
            stderr=sys.stderr)
        wait(process)

    @unittest.skipIf(os.name == 'nt', 'fork not supported on windows')
    def test_server_fork_can_exit(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_server_shutdown_scenarios.SERVER_FORK_CAN_EXIT],
            stdout=sys.stdout,
            stderr=sys.stderr)
        wait(process)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
