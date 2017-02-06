# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Tests clean exit of server/client on Python Interpreter exit/sigint.

The tests in this module spawn a subprocess for each test case, the
test is considered successful if it doesn't hang/timeout.
"""

import atexit
import os
import signal
import six
import subprocess
import sys
import threading
import time
import unittest

from tests.unit import _exit_scenarios

SCENARIO_FILE = os.path.abspath(
    os.path.join(
        os.path.dirname(os.path.realpath(__file__)), '_exit_scenarios.py'))
INTERPRETER = sys.executable
BASE_COMMAND = [INTERPRETER, SCENARIO_FILE]
BASE_SIGTERM_COMMAND = BASE_COMMAND + ['--wait_for_interrupt']

INIT_TIME = 1.0

processes = []
process_lock = threading.Lock()


# Make sure we attempt to clean up any
# processes we may have left running
def cleanup_processes():
    with process_lock:
        for process in processes:
            try:
                process.kill()
            except Exception:
                pass


atexit.register(cleanup_processes)


def interrupt_and_wait(process):
    with process_lock:
        processes.append(process)
    time.sleep(INIT_TIME)
    os.kill(process.pid, signal.SIGINT)
    process.wait()


def wait(process):
    with process_lock:
        processes.append(process)
    process.wait()


@unittest.skip('https://github.com/grpc/grpc/issues/7311')
class ExitTest(unittest.TestCase):

    def test_unstarted_server(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.UNSTARTED_SERVER],
            stdout=sys.stdout,
            stderr=sys.stderr)
        wait(process)

    def test_unstarted_server_terminate(self):
        process = subprocess.Popen(
            BASE_SIGTERM_COMMAND + [_exit_scenarios.UNSTARTED_SERVER],
            stdout=sys.stdout)
        interrupt_and_wait(process)

    def test_running_server(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.RUNNING_SERVER],
            stdout=sys.stdout,
            stderr=sys.stderr)
        wait(process)

    def test_running_server_terminate(self):
        process = subprocess.Popen(
            BASE_SIGTERM_COMMAND + [_exit_scenarios.RUNNING_SERVER],
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
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.POLL_CONNECTIVITY],
            stdout=sys.stdout,
            stderr=sys.stderr)
        wait(process)

    def test_poll_connectivity_terminate(self):
        process = subprocess.Popen(
            BASE_SIGTERM_COMMAND + [_exit_scenarios.POLL_CONNECTIVITY],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    def test_in_flight_unary_unary_call(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.IN_FLIGHT_UNARY_UNARY_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(six.PY2, 'https://github.com/grpc/grpc/issues/6999')
    def test_in_flight_unary_stream_call(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.IN_FLIGHT_UNARY_STREAM_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    def test_in_flight_stream_unary_call(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.IN_FLIGHT_STREAM_UNARY_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(six.PY2, 'https://github.com/grpc/grpc/issues/6999')
    def test_in_flight_stream_stream_call(self):
        process = subprocess.Popen(
            BASE_COMMAND + [_exit_scenarios.IN_FLIGHT_STREAM_STREAM_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(six.PY2, 'https://github.com/grpc/grpc/issues/6999')
    def test_in_flight_partial_unary_stream_call(self):
        process = subprocess.Popen(
            BASE_COMMAND +
            [_exit_scenarios.IN_FLIGHT_PARTIAL_UNARY_STREAM_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    def test_in_flight_partial_stream_unary_call(self):
        process = subprocess.Popen(
            BASE_COMMAND +
            [_exit_scenarios.IN_FLIGHT_PARTIAL_STREAM_UNARY_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)

    @unittest.skipIf(six.PY2, 'https://github.com/grpc/grpc/issues/6999')
    def test_in_flight_partial_stream_stream_call(self):
        process = subprocess.Popen(
            BASE_COMMAND +
            [_exit_scenarios.IN_FLIGHT_PARTIAL_STREAM_STREAM_CALL],
            stdout=sys.stdout,
            stderr=sys.stderr)
        interrupt_and_wait(process)


if __name__ == '__main__':
    unittest.main(verbosity=2)
