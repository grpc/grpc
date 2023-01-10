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
from typing import Any, Callable, Optional, Sequence, Tuple

import grpc
from grpc._typing import MetadataType
from grpc_testing import _common

_CLIENT_INACTIVE = object()


class Handler(_common.ServerRpcHandler):

    @abc.abstractmethod
    def initial_metadata(self) -> Optional[MetadataType]:
        raise NotImplementedError()

    @abc.abstractmethod
    def add_request(self, request: Any) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def take_response(self) -> Any:
        raise NotImplementedError()

    @abc.abstractmethod
    def requests_closed(self) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def cancel(self) -> None:
        raise NotImplementedError()

    @abc.abstractmethod
    def unary_response_termination(
            self) -> Tuple[Any, Optional[MetadataType], grpc.StatusCode, str]:
        raise NotImplementedError()

    @abc.abstractmethod
    def stream_response_termination(
            self) -> Tuple[Optional[MetadataType], grpc.StatusCode, str]:
        raise NotImplementedError()


class _Handler(Handler):
    _condition: threading.Condition
    _requests = Sequence
    _requests_closed = bool
    _initial_metadata = Optional[MetadataType]
    _responses = Sequence
    _trailing_metadata = Optional[MetadataType]
    _code = Optional[grpc.StatusCode]
    _details = Optional[str]
    _unary_response = Any
    _expiration_future = Optional[grpc.Future]
    _termination_callbacks = Sequence[Callable[[], None]]

    def __init__(self, requests_closed: bool):
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

    def send_initial_metadata(self,
                              initial_metadata: Optional[MetadataType]) -> None:
        with self._condition:
            self._initial_metadata = initial_metadata
            self._condition.notify_all()

    def take_request(self) -> _common.ServerRpcRead:
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

    def is_active(self) -> bool:
        with self._condition:
            return self._code is None

    def add_response(self, response: Any) -> None:
        with self._condition:
            self._responses.append(response)
            self._condition.notify_all()

    def send_termination(self, trailing_metadata: Optional[MetadataType],
                         code: grpc.StatusCode, details: str) -> None:
        with self._condition:
            self._trailing_metadata = trailing_metadata
            self._code = code
            self._details = details
            if self._expiration_future is not None:
                self._expiration_future.cancel()
            self._condition.notify_all()

    def add_termination_callback(self, callback: Callable[[], None]) -> bool:
        with self._condition:
            if self._code is None:
                self._termination_callbacks.append(callback)
                return True
            else:
                return False

    def initial_metadata(self) -> Optional[MetadataType]:
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

    def add_request(self, request: Any) -> None:
        with self._condition:
            self._requests.append(request)
            self._condition.notify_all()

    def take_response(self) -> Any:
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

    def requests_closed(self) -> None:
        with self._condition:
            self._requests_closed = True
            self._condition.notify_all()

    def cancel(self) -> None:
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

    def unary_response_termination(
            self) -> Tuple[Any, Optional[MetadataType], grpc.StatusCode, str]:
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

    def stream_response_termination(
            self) -> Tuple[Optional[MetadataType], grpc.StatusCode, str]:
        with self._condition:
            while True:
                if self._code is _CLIENT_INACTIVE:
                    raise ValueError('Huh? Cancelled but wanting status?')
                elif self._code is None:
                    self._condition.wait()
                else:
                    return self._trailing_metadata, self._code, self._details

    def expire(self) -> None:
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

    def set_expiration_future(self, expiration_future: grpc.Future) -> None:
        with self._condition:
            self._expiration_future = expiration_future


def handler_without_deadline(requests_closed: bool) -> _Handler:
    return _Handler(requests_closed)


def handler_with_deadline(requests_closed: bool, time: float,
                          deadline: float) -> _Handler:
    handler = _Handler(requests_closed)
    expiration_future = time.call_at(handler.expire, deadline)
    handler.set_expiration_future(expiration_future)
    return handler
