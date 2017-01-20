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
