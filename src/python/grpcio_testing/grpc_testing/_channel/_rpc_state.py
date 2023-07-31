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

import threading

import grpc
from grpc_testing import _common


class State(_common.ChannelRpcHandler):
    def __init__(self, invocation_metadata, requests, requests_closed):
        self._condition = threading.Condition()
        self._invocation_metadata = invocation_metadata
        self._requests = requests
        self._requests_closed = requests_closed
        self._initial_metadata = None
        self._responses = []
        self._trailing_metadata = None
        self._code = None
        self._details = None

    def initial_metadata(self):
        with self._condition:
            while True:
                if self._initial_metadata is None:
                    if self._code is None:
                        self._condition.wait()
                    else:
                        return _common.FUSSED_EMPTY_METADATA
                else:
                    return self._initial_metadata

    def add_request(self, request):
        with self._condition:
            if self._code is None and not self._requests_closed:
                self._requests.append(request)
                self._condition.notify_all()
                return True
            else:
                return False

    def close_requests(self):
        with self._condition:
            if self._code is None and not self._requests_closed:
                self._requests_closed = True
                self._condition.notify_all()

    def take_response(self):
        with self._condition:
            while True:
                if self._code is grpc.StatusCode.OK:
                    if self._responses:
                        response = self._responses.pop(0)
                        return _common.ChannelRpcRead(
                            response, None, None, None
                        )
                    else:
                        return _common.ChannelRpcRead(
                            None,
                            self._trailing_metadata,
                            grpc.StatusCode.OK,
                            self._details,
                        )
                elif self._code is None:
                    if self._responses:
                        response = self._responses.pop(0)
                        return _common.ChannelRpcRead(
                            response, None, None, None
                        )
                    else:
                        self._condition.wait()
                else:
                    return _common.ChannelRpcRead(
                        None, self._trailing_metadata, self._code, self._details
                    )

    def termination(self):
        with self._condition:
            while True:
                if self._code is None:
                    self._condition.wait()
                else:
                    return self._trailing_metadata, self._code, self._details

    def cancel(self, code, details):
        with self._condition:
            if self._code is None:
                if self._initial_metadata is None:
                    self._initial_metadata = _common.FUSSED_EMPTY_METADATA
                self._trailing_metadata = _common.FUSSED_EMPTY_METADATA
                self._code = code
                self._details = details
                self._condition.notify_all()
                return True
            else:
                return False

    def take_invocation_metadata(self):
        with self._condition:
            if self._invocation_metadata is None:
                raise ValueError("Expected invocation metadata!")
            else:
                invocation_metadata = self._invocation_metadata
                self._invocation_metadata = None
                return invocation_metadata

    def take_invocation_metadata_and_request(self):
        with self._condition:
            if self._invocation_metadata is None:
                raise ValueError("Expected invocation metadata!")
            elif not self._requests:
                raise ValueError("Expected at least one request!")
            else:
                invocation_metadata = self._invocation_metadata
                self._invocation_metadata = None
                return invocation_metadata, self._requests.pop(0)

    def send_initial_metadata(self, initial_metadata):
        with self._condition:
            self._initial_metadata = _common.fuss_with_metadata(
                initial_metadata
            )
            self._condition.notify_all()

    def take_request(self):
        with self._condition:
            while True:
                if self._requests:
                    return self._requests.pop(0)
                else:
                    self._condition.wait()

    def requests_closed(self):
        with self._condition:
            while True:
                if self._requests_closed:
                    return
                else:
                    self._condition.wait()

    def send_response(self, response):
        with self._condition:
            if self._code is None:
                self._responses.append(response)
                self._condition.notify_all()

    def terminate_with_response(
        self, response, trailing_metadata, code, details
    ):
        with self._condition:
            if self._initial_metadata is None:
                self._initial_metadata = _common.FUSSED_EMPTY_METADATA
            self._responses.append(response)
            self._trailing_metadata = _common.fuss_with_metadata(
                trailing_metadata
            )
            self._code = code
            self._details = details
            self._condition.notify_all()

    def terminate(self, trailing_metadata, code, details):
        with self._condition:
            if self._initial_metadata is None:
                self._initial_metadata = _common.FUSSED_EMPTY_METADATA
            self._trailing_metadata = _common.fuss_with_metadata(
                trailing_metadata
            )
            self._code = code
            self._details = details
            self._condition.notify_all()

    def cancelled(self):
        with self._condition:
            while True:
                if self._code is grpc.StatusCode.CANCELLED:
                    return
                elif self._code is None:
                    self._condition.wait()
                else:
                    raise ValueError(
                        f"Status code unexpectedly {self._code}!"
                    )

    def is_active(self):
        raise NotImplementedError()

    def time_remaining(self):
        raise NotImplementedError()

    def add_callback(self, callback):
        raise NotImplementedError()
