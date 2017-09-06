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
"""Defines behavior for WHEN clients send requests.

Each client exposes a non-blocking send_request() method that the
ClientRunner invokes either periodically or in response to some event.
"""

import abc
import threading
import time


class ClientRunner:
    """Abstract interface for sending requests from clients."""

    __metaclass__ = abc.ABCMeta

    def __init__(self, client):
        self._client = client

    @abc.abstractmethod
    def start(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def stop(self):
        raise NotImplementedError()


class OpenLoopClientRunner(ClientRunner):

    def __init__(self, client, interval_generator):
        super(OpenLoopClientRunner, self).__init__(client)
        self._is_running = False
        self._interval_generator = interval_generator
        self._dispatch_thread = threading.Thread(
            target=self._dispatch_requests, args=())

    def start(self):
        self._is_running = True
        self._client.start()
        self._dispatch_thread.start()

    def stop(self):
        self._is_running = False
        self._client.stop()
        self._dispatch_thread.join()
        self._client = None

    def _dispatch_requests(self):
        while self._is_running:
            self._client.send_request()
            time.sleep(next(self._interval_generator))


class ClosedLoopClientRunner(ClientRunner):

    def __init__(self, client, request_count):
        super(ClosedLoopClientRunner, self).__init__(client)
        self._is_running = False
        self._request_count = request_count
        # Send a new request on each response for closed loop
        self._client.add_response_callback(self._send_request)

    def start(self):
        self._is_running = True
        self._client.start()
        for _ in xrange(self._request_count):
            self._client.send_request()

    def stop(self):
        self._is_running = False
        self._client.stop()
        self._client = None

    def _send_request(self, client, response_time):
        if self._is_running:
            client.send_request()
