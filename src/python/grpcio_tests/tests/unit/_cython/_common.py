# Copyright 2017 gRPC authors.
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
"""Common utilities for tests of the Cython layer of gRPC Python."""

import collections
import threading

from grpc._cython import cygrpc

RPC_COUNT = 4000

EMPTY_FLAGS = 0

INVOCATION_METADATA = (
    ("client-md-key", "client-md-key"),
    ("client-md-key-bin", b"\x00\x01" * 3000),
)

INITIAL_METADATA = (
    ("server-initial-md-key", "server-initial-md-value"),
    ("server-initial-md-key-bin", b"\x00\x02" * 3000),
)

TRAILING_METADATA = (
    ("server-trailing-md-key", "server-trailing-md-value"),
    ("server-trailing-md-key-bin", b"\x00\x03" * 3000),
)


class QueueDriver(object):
    def __init__(self, condition, completion_queue):
        self._condition = condition
        self._completion_queue = completion_queue
        self._due = collections.defaultdict(int)
        self._events = collections.defaultdict(list)

    def add_due(self, tags):
        if not self._due:

            def in_thread():
                while True:
                    event = self._completion_queue.poll()
                    with self._condition:
                        self._events[event.tag].append(event)
                        self._due[event.tag] -= 1
                        self._condition.notify_all()
                        if self._due[event.tag] <= 0:
                            self._due.pop(event.tag)
                            if not self._due:
                                return

            thread = threading.Thread(target=in_thread)
            thread.start()
        for tag in tags:
            self._due[tag] += 1

    def event_with_tag(self, tag):
        with self._condition:
            while True:
                if self._events[tag]:
                    return self._events[tag].pop(0)
                else:
                    self._condition.wait()


def execute_many_times(behavior):
    return tuple(behavior() for _ in range(RPC_COUNT))


class OperationResult(
    collections.namedtuple(
        "OperationResult",
        (
            "start_batch_result",
            "completion_type",
            "success",
        ),
    )
):
    pass


SUCCESSFUL_OPERATION_RESULT = OperationResult(
    cygrpc.CallError.ok, cygrpc.CompletionType.operation_complete, True
)


class RpcTest(object):
    def setUp(self):
        self.server_completion_queue = cygrpc.CompletionQueue()
        self.server = cygrpc.Server([(b"grpc.so_reuseport", 0)], False)
        self.server.register_completion_queue(self.server_completion_queue)
        port = self.server.add_http2_port(b"[::]:0")
        self.server.start()
        self.channel = cygrpc.Channel(
            "localhost:{}".format(port).encode(), [], None
        )

        self._server_shutdown_tag = "server_shutdown_tag"
        self.server_condition = threading.Condition()
        self.server_driver = QueueDriver(
            self.server_condition, self.server_completion_queue
        )
        with self.server_condition:
            self.server_driver.add_due(
                {
                    self._server_shutdown_tag,
                }
            )

        self.client_condition = threading.Condition()
        self.client_completion_queue = cygrpc.CompletionQueue()
        self.client_driver = QueueDriver(
            self.client_condition, self.client_completion_queue
        )

    def tearDown(self):
        self.server.shutdown(
            self.server_completion_queue, self._server_shutdown_tag
        )
        self.server.cancel_all_calls()
