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
"""Shared test harness for concurrency test suites.

Meant to run on a free-threaded interpreter, ideally TSAN instrumented
(--config=tsan_python): data races surface as ThreadSanitizer reports, deadlocks
as join timeouts.
"""
import threading
import traceback
import unittest

import grpc

from concurrency_tests._deadlock_debug import install_deadlock_debuggers
from tests.unit import test_common


DEFAULT_THREAD_COUNT = 100
ITERATIONS_PER_THREAD = 10
_DEFAULT_WORKER_START_TIMEOUT = 60
_DEFAULT_WORKER_JOIN_TIMEOUT = 90


class ConcurrencyTestCase(unittest.TestCase):
    """Base class providing the concurrent worker harness"""

    thread_count = DEFAULT_THREAD_COUNT

    def setUp(self) -> None:
        super().setUp()
        install_deadlock_debuggers(self.addCleanup)
        self._threads = []
        self._errors = []
        self._barrier = threading.Barrier(self.thread_count)

    def tearDown(self) -> None:
        stuck = []
        for t in self._threads:
            t.join(timeout=_DEFAULT_WORKER_JOIN_TIMEOUT)
            if t.is_alive():
                stuck.append(t)
        super().tearDown()
        self.assertEqual([], stuck, f"deadlocked worker threads: {stuck}")
        self.assertEqual([], self._errors, "\n\n".join(self._errors))

    def start_server(self, servicer, add_to_server, stub_class):
        """Starts insecure server for the servicer; returns (server, stub)

        The channel and server are torn down via addCleanup, after tearDown's
        join, so a hang is detected before anything is stopped.
        """
        server = test_common.test_server(max_workers=self.thread_count)
        add_to_server(servicer, server)
        port = server.add_insecure_port("[::]:0")
        server.start()
        channel = grpc.insecure_channel(f"localhost:{port}")
        self.addCleanup(server.stop, None)
        self.addCleanup(channel.close)
        return server, stub_class(channel)

    def _worker(self, target, index):
        try:
            self._barrier.wait(timeout=_DEFAULT_WORKER_START_TIMEOUT)
            target(index)
        except Exception:  # pylint: disable=broad-except
            self._errors.append(traceback.format_exc())

    def spawn_workers(self, *targets):
        """Starts `thread_count` workers, distribtued round robin over targets"""
        for index in range(self.thread_count):
            thread = threading.Thread(
                target=self._worker,
                args=(targets[index % len(targets)], index),
                name=f"worker_{index}",
            )
            self._threads.append(thread)
            thread.start()