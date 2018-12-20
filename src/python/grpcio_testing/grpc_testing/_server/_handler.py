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

import abc
import threading

import grpc
from grpc_testing import _common

_CLIENT_INACTIVE = object()


class Handler(_common.ServerRpcHandler):

    @abc.abstractmethod
    def initial_metadata(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def add_request(self, request):
        raise NotImplementedError()

    @abc.abstractmethod
    def take_response(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def requests_closed(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def cancel(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def unary_response_termination(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def stream_response_termination(self):
        raise NotImplementedError()


class _Handler(Handler):

    def __init__(self, requests_closed):
        self._condition = threading.Condition()
        self._requests = []
        self._requests_closed = requests_closed
        self._initial_metadata = None
        self._responses = []
        self._trailing_metadata = None
        self._code = None
        self._details = None
        self._unary_response = None
        self._expiration_future = None
        self._termination_callbacks = []

    def send_initial_metadata(self, initial_metadata):
        with self._condition:
            self._initial_metadata = initial_metadata
            self._condition.notify_all()

    def take_request(self):
        with self._condition:
            while True:
                if self._code is None:
                    if self._requests:
                        request = self._requests.pop(0)
                        self._condition.notify_all()
                        return _common.ServerRpcRead(request, False, False)
                    elif self._requests_closed:
                        return _common.REQUESTS_CLOSED
                    else:
                        self._condition.wait()
                else:
                    return _common.TERMINATED

    def is_active(self):
        with self._condition:
            return self._code is None

    def add_response(self, response):
        with self._condition:
            self._responses.append(response)
            self._condition.notify_all()

    def send_termination(self, trailing_metadata, code, details):
        with self._condition:
            self._trailing_metadata = trailing_metadata
            self._code = code
            self._details = details
            if self._expiration_future is not None:
                self._expiration_future.cancel()
            self._condition.notify_all()

    def add_termination_callback(self, callback):
        with self._condition:
            if self._code is None:
                self._termination_callbacks.append(callback)
                return True
            else:
                return False

    def initial_metadata(self):
        with self._condition:
            while True:
                if self._initial_metadata is None:
                    if self._code is None:
                        self._condition.wait()
                    else:
                        raise ValueError(
                            'No initial metadata despite status code!')
                else:
                    return self._initial_metadata

    def add_request(self, request):
        with self._condition:
            self._requests.append(request)
            self._condition.notify_all()

    def take_response(self):
        with self._condition:
            while True:
                if self._responses:
                    response = self._responses.pop(0)
                    self._condition.notify_all()
                    return response
                elif self._code is None:
                    self._condition.wait()
                else:
                    raise ValueError('No more responses!')

    def requests_closed(self):
        with self._condition:
            self._requests_closed = True
            self._condition.notify_all()

    def cancel(self):
        with self._condition:
            if self._code is None:
                self._code = _CLIENT_INACTIVE
                termination_callbacks = self._termination_callbacks
                self._termination_callbacks = None
                if self._expiration_future is not None:
                    self._expiration_future.cancel()
                self._condition.notify_all()
        for termination_callback in termination_callbacks:
            termination_callback()

    def unary_response_termination(self):
        with self._condition:
            while True:
                if self._code is _CLIENT_INACTIVE:
                    raise ValueError('Huh? Cancelled but wanting status?')
                elif self._code is None:
                    self._condition.wait()
                else:
                    if self._unary_response is None:
                        if self._responses:
                            self._unary_response = self._responses.pop(0)
                    return (
                        self._unary_response,
                        self._trailing_metadata,
                        self._code,
                        self._details,
                    )

    def stream_response_termination(self):
        with self._condition:
            while True:
                if self._code is _CLIENT_INACTIVE:
                    raise ValueError('Huh? Cancelled but wanting status?')
                elif self._code is None:
                    self._condition.wait()
                else:
                    return self._trailing_metadata, self._code, self._details

    def expire(self):
        with self._condition:
            if self._code is None:
                if self._initial_metadata is None:
                    self._initial_metadata = _common.FUSSED_EMPTY_METADATA
                self._trailing_metadata = _common.FUSSED_EMPTY_METADATA
                self._code = grpc.StatusCode.DEADLINE_EXCEEDED
                self._details = 'Took too much time!'
                termination_callbacks = self._termination_callbacks
                self._termination_callbacks = None
                self._condition.notify_all()
        for termination_callback in termination_callbacks:
            termination_callback()

    def set_expiration_future(self, expiration_future):
        with self._condition:
            self._expiration_future = expiration_future


def handler_without_deadline(requests_closed):
    return _Handler(requests_closed)


def handler_with_deadline(requests_closed, time, deadline):
    handler = _Handler(requests_closed)
    expiration_future = time.call_at(handler.expire, deadline)
    handler.set_expiration_future(expiration_future)
    return handler
