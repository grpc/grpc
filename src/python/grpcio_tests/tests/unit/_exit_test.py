# Copyright 2016 gRPC authors.
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
"""Tests clean exit of server/client on Python Interpreter exit/sigint.

The tests in this module spawn a subprocess for each test case, the
test is considered successful if it doesn't freeze/timeout.
"""

import atexit
import os
import signal
import six
import subprocess
import sys
import threading
import datetime
import time
import unittest
import logging

from tests.unit import _exit_scenarios

SCENARIO_FILE = os.path.abspath(
    os.path.join(os.path.dirname(os.path.realpath(__file__)),
                 '_exit_scenarios.py'))
INTERPRETER = sys.executable
BASE_COMMAND = [INTERPRETER, SCENARIO_FILE]
BASE_SIGTERM_COMMAND = BASE_COMMAND + ['--wait_for_interrupt']

INIT_TIME = datetime.timedelta(seconds=1)
WAIT_CHECK_INTERVAL = datetime.timedelta(milliseconds=100)
WAIT_CHECK_DEFAULT_TIMEOUT = datetime.timedelta(seconds=5)

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


def _process_wait_with_timeout(process, timeout=WAIT_CHECK_DEFAULT_TIMEOUT):
    """A funciton to mimic 3.3+ only timeout argument in process.wait."""
    deadline = datetime.datetime.now() + timeout
    while (process.poll() is None) and (datetime.datetime.now() < deadline):
        time.sleep(WAIT_CHECK_INTERVAL.total_seconds())
    if process.returncode is None:
        raise RuntimeError('Process failed to exit within %s' % timeout)


def interrupt_and_wait(process):
    with process_lock:
        processes.append(process)
    time.sleep(INIT_TIME.total_seconds())
    os.kill(process.pid, signal.SIGINT)
    _process_wait_with_timeout(process)


def wait(process):
    with process_lock:
        processes.append(process)
    _process_wait_with_timeout(process)


# TODO(lidiz) enable exit tests once the root cause found.
@unittest.skip('https://github.com/grpc/grpc/issues/23982')
@unittest.skip('https://github.com/grpc/grpc/issues/23028')
class ExitTest(unittest.TestCase):

    def test_unstarted_server(self):
        process = subprocess.Popen(BASE_COMMAND +
                                   [_exit_scenarios.UNSTARTED_SERVER],
                                   stdout=sys.stdout,
                                   stderr=sys.stderr)
        wait(process)

    def test_unstarted_server_terminate(self):
        process = subprocess.Popen(BASE_SIGTERM_COMMAND +
                                   [_exit_scenarios.UNSTARTED_SERVER],
                                   stdout=sys.stdout)
        interrupt_and_wait(process)

    def test_running_server(self):
        process = subprocess.Popen(BASE_COMMAND +
                                   [_exit_scenarios.RUNNING_SERVER],
                                   stdout=sys.stdout,
                                   stderr=sys.stderr)
        wait(process)

    def test_running_server_terminate(self):
        process = subprocess.Popen(BASE_SIGTERM_COMMAND +
                                   [_exit_scenarios.RUNNING_SERVER],
                                   stdout=sys.stdout,
                                   stderr=sys.stderr)
        interrupt_and_wait(process)

    def test_poll_connectivity_no_server(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.POLL_CONNECTIVITY_NO_SERVER],
            stdout=sys.stdout,
            stderr=sys.stderr)
        wait(process)

    def test_poll_connectivity_no_server_terminate(self):
        process = subprocess.Popen(
            BASE_SIGTERM_COMMAND +
            [_exit_scenarios.POLL_CONNECTIVITY_NO_SERVER],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    def test_poll_connectivity(self):
        process = subprocess.Popen(BASE_COMMAND +
                                   [_exit_scenarios.POLL_CONNECTIVITY],
                                   stdout=sys.stdout,
                                   stderr=sys.stderr)
        wait(process)

    def test_poll_connectivity_terminate(self):
        process = subprocess.Popen(BASE_SIGTERM_COMMAND +
                                   [_exit_scenarios.POLL_CONNECTIVITY],
                                   stdout=sys.stdout,
                                   stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(os.name == 'nt',
                     'os.kill does not have required permission on Windows')
    def test_in_flight_unary_unary_call(self):
        process = subprocess.Popen(BASE_COMMAND +
                                   [_exit_scenarios.IN_FLIGHT_UNARY_UNARY_CALL],
                                   stdout=sys.stdout,
                                   stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(os.name == 'nt',
                     'os.kill does not have required permission on Windows')
    def test_in_flight_unary_stream_call(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.IN_FLIGHT_UNARY_STREAM_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(os.name == 'nt',
                     'os.kill does not have required permission on Windows')
    def test_in_flight_stream_unary_call(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.IN_FLIGHT_STREAM_UNARY_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(os.name == 'nt',
                     'os.kill does not have required permission on Windows')
    def test_in_flight_stream_stream_call(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.IN_FLIGHT_STREAM_STREAM_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(os.name == 'nt',
                     'os.kill does not have required permission on Windows')
    def test_in_flight_partial_unary_stream_call(self):
        process = subprocess.Popen(
            BASE_COMMAND +
            [_exit_scenarios.IN_FLIGHT_PARTIAL_UNARY_STREAM_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(os.name == 'nt',
                     'os.kill does not have required permission on Windows')
    def test_in_flight_partial_stream_unary_call(self):
        process = subprocess.Popen(
            BASE_COMMAND +
            [_exit_scenarios.IN_FLIGHT_PARTIAL_STREAM_UNARY_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(os.name == 'nt',
                     'os.kill does not have required permission on Windows')
    def test_in_flight_partial_stream_stream_call(self):
        process = subprocess.Popen(
            BASE_COMMAND +
            [_exit_scenarios.IN_FLIGHT_PARTIAL_STREAM_STREAM_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
