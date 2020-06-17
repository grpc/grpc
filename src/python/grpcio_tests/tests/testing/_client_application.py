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
"""An example gRPC Python-using client-side application."""

import collections
import enum
import threading
import time

import grpc
from tests.unit.framework.common import test_constants

from tests.testing.proto import requests_pb2
from tests.testing.proto import services_pb2
from tests.testing.proto import services_pb2_grpc

from tests.testing import _application_common


@enum.unique
class Scenario(enum.Enum):
    UNARY_UNARY = 'unary unary'
    UNARY_STREAM = 'unary stream'
    STREAM_UNARY = 'stream unary'
    STREAM_STREAM = 'stream stream'
    CONCURRENT_STREAM_UNARY = 'concurrent stream unary'
    CONCURRENT_STREAM_STREAM = 'concurrent stream stream'
    CANCEL_UNARY_UNARY = 'cancel unary unary'
    CANCEL_UNARY_STREAM = 'cancel unary stream'
    INFINITE_REQUEST_STREAM = 'infinite request stream'


class Outcome(collections.namedtuple('Outcome', ('kind', 'code', 'details'))):
    """Outcome of a client application scenario.

    Attributes:
      kind: A Kind value describing the overall kind of scenario execution.
      code: A grpc.StatusCode value. Only valid if kind is Kind.RPC_ERROR.
      details: A status details string. Only valid if kind is Kind.RPC_ERROR.
    """

    @enum.unique
    class Kind(enum.Enum):
        SATISFACTORY = 'satisfactory'
        UNSATISFACTORY = 'unsatisfactory'
        RPC_ERROR = 'rpc error'


_SATISFACTORY_OUTCOME = Outcome(Outcome.Kind.SATISFACTORY, None, None)
_UNSATISFACTORY_OUTCOME = Outcome(Outcome.Kind.UNSATISFACTORY, None, None)


class _Pipe(object):

    def __init__(self):
        self._condition = threading.Condition()
        self._values = []
        self._open = True

    def __iter__(self):
        return self

    def _next(self):
        with self._condition:
            while True:
                if self._values:
                    return self._values.pop(0)
                elif not self._open:
                    raise StopIteration()
                else:
                    self._condition.wait()

    def __next__(self):  # (Python 3 Iterator Protocol)
        return self._next()

    def next(self):  # (Python 2 Iterator Protocol)
        return self._next()

    def add(self, value):
        with self._condition:
            self._values.append(value)
            self._condition.notify_all()

    def close(self):
        with self._condition:
            self._open = False
            self._condition.notify_all()


def _run_unary_unary(stub):
    response = stub.UnUn(_application_common.UNARY_UNARY_REQUEST)
    if _application_common.UNARY_UNARY_RESPONSE == response:
        return _SATISFACTORY_OUTCOME
    else:
        return _UNSATISFACTORY_OUTCOME


def _run_unary_stream(stub):
    response_iterator = stub.UnStre(_application_common.UNARY_STREAM_REQUEST)
    try:
        next(response_iterator)
    except StopIteration:
        return _SATISFACTORY_OUTCOME
    else:
        return _UNSATISFACTORY_OUTCOME


def _run_stream_unary(stub):
    response, call = stub.StreUn.with_call(
        iter((_application_common.STREAM_UNARY_REQUEST,) * 3))
    if (_application_common.STREAM_UNARY_RESPONSE == response and
            call.code() is grpc.StatusCode.OK):
        return _SATISFACTORY_OUTCOME
    else:
        return _UNSATISFACTORY_OUTCOME


def _run_stream_stream(stub):
    request_pipe = _Pipe()
    response_iterator = stub.StreStre(iter(request_pipe))
    request_pipe.add(_application_common.STREAM_STREAM_REQUEST)
    first_responses = next(response_iterator), next(response_iterator)
    request_pipe.add(_application_common.STREAM_STREAM_REQUEST)
    second_responses = next(response_iterator), next(response_iterator)
    request_pipe.close()
    try:
        next(response_iterator)
    except StopIteration:
        unexpected_extra_response = False
    else:
        unexpected_extra_response = True
    if (first_responses == _application_common.TWO_STREAM_STREAM_RESPONSES and
            second_responses == _application_common.TWO_STREAM_STREAM_RESPONSES
            and not unexpected_extra_response):
        return _SATISFACTORY_OUTCOME
    else:
        return _UNSATISFACTORY_OUTCOME


def _run_concurrent_stream_unary(stub):
    future_calls = tuple(
        stub.StreUn.future(iter((_application_common.STREAM_UNARY_REQUEST,) *
                                3))
        for _ in range(test_constants.THREAD_CONCURRENCY))
    for future_call in future_calls:
        if future_call.code() is grpc.StatusCode.OK:
            response = future_call.result()
            if _application_common.STREAM_UNARY_RESPONSE != response:
                return _UNSATISFACTORY_OUTCOME
        else:
            return _UNSATISFACTORY_OUTCOME
    else:
        return _SATISFACTORY_OUTCOME


def _run_concurrent_stream_stream(stub):
    condition = threading.Condition()
    outcomes = [None] * test_constants.RPC_CONCURRENCY

    def run_stream_stream(index):
        outcome = _run_stream_stream(stub)
        with condition:
            outcomes[index] = outcome
            condition.notify()

    for index in range(test_constants.RPC_CONCURRENCY):
        thread = threading.Thread(target=run_stream_stream, args=(index,))
        thread.start()
    with condition:
        while True:
            if all(outcomes):
                for outcome in outcomes:
                    if outcome.kind is not Outcome.Kind.SATISFACTORY:
                        return _UNSATISFACTORY_OUTCOME
                else:
                    return _SATISFACTORY_OUTCOME
            else:
                condition.wait()


def _run_cancel_unary_unary(stub):
    response_future_call = stub.UnUn.future(
        _application_common.UNARY_UNARY_REQUEST)
    initial_metadata = response_future_call.initial_metadata()
    cancelled = response_future_call.cancel()
    if initial_metadata is not None and cancelled:
        return _SATISFACTORY_OUTCOME
    else:
        return _UNSATISFACTORY_OUTCOME


def _run_infinite_request_stream(stub):

    def infinite_request_iterator():
        while True:
            yield _application_common.STREAM_UNARY_REQUEST

    response_future_call = stub.StreUn.future(
        infinite_request_iterator(),
        timeout=_application_common.INFINITE_REQUEST_STREAM_TIMEOUT)
    if response_future_call.code() is grpc.StatusCode.DEADLINE_EXCEEDED:
        return _SATISFACTORY_OUTCOME
    else:
        return _UNSATISFACTORY_OUTCOME


_IMPLEMENTATIONS = {
    Scenario.UNARY_UNARY: _run_unary_unary,
    Scenario.UNARY_STREAM: _run_unary_stream,
    Scenario.STREAM_UNARY: _run_stream_unary,
    Scenario.STREAM_STREAM: _run_stream_stream,
    Scenario.CONCURRENT_STREAM_UNARY: _run_concurrent_stream_unary,
    Scenario.CONCURRENT_STREAM_STREAM: _run_concurrent_stream_stream,
    Scenario.CANCEL_UNARY_UNARY: _run_cancel_unary_unary,
    Scenario.INFINITE_REQUEST_STREAM: _run_infinite_request_stream,
}


def run(scenario, channel):
    stub = services_pb2_grpc.FirstServiceStub(channel)
    try:
        return _IMPLEMENTATIONS[scenario](stub)
    except grpc.RpcError as rpc_error:
        return Outcome(Outcome.Kind.RPC_ERROR, rpc_error.code(),
                       rpc_error.details())
