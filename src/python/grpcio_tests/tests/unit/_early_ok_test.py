# Copyright 2018 gRPC authors.
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
"""Tests servicers sending OK status without having read all requests.

This is a regression test of https://github.com/grpc/grpc/issues/6891.
"""

import enum
import unittest

import six

import grpc

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_RPC_METHOD = '/serffice/Meffod'


@enum.unique
class _MessageCount(enum.Enum):

    ZERO = (
        0,
        'Zero',
    )
    TWO = (
        1,
        'Two',
    )
    MANY = (
        test_constants.STREAM_LENGTH,
        'Many',
    )


@enum.unique
class _MessageSize(enum.Enum):
    EMPTY = (
        0,
        'Empty',
    )
    SMALL = (
        32,
        'Small',
    )  # Smaller than any flow control window.
    LARGE = (
        3 * 1024 * 1024,
        'Large',
    )  # Larger than any flow control window.


_ZERO_MESSAGE = b''
_SMALL_MESSAGE = b'\x07' * _MessageSize.SMALL.value[0]
_LARGE_MESSAGE = b'abc' * (_MessageSize.LARGE.value[0] // 3)


@enum.unique
class _ReadRequests(enum.Enum):

    ZERO = (
        0,
        'Zero',
    )
    TWO = (
        2,
        'Two',
    )


class _Case(object):

    def __init__(self, request_count, request_size, request_reading,
                 response_count, response_size):
        self.request_count = request_count
        self.request_size = request_size
        self.request_reading = request_reading
        self.response_count = response_count
        self.response_size = response_size

    def create_test_case_name(self):
        return '{}{}Requests{}Read{}{}ResponsesEarlyOKTest'.format(
            self.request_count.value[1], self.request_size.value[1],
            self.request_reading.value[1], self.response_count.value[1],
            self.response_size.value[1])


def _message(message_size):
    if message_size is _MessageSize.EMPTY:
        return _ZERO_MESSAGE
    elif message_size is _MessageSize.SMALL:
        return _SMALL_MESSAGE
    elif message_size is _MessageSize.LARGE:
        return _LARGE_MESSAGE


def _messages_to_send(count, size):
    for _ in range(count.value[0]):
        yield _message(size)


def _draw_requests(case, request_iterator):
    for _ in range(
            min(case.request_count.value[0], case.request_reading.value[0])):
        next(request_iterator)


def _draw_responses(case, response_iterator):
    for _ in range(case.response_count.value[0]):
        next(response_iterator)


class _MethodHandler(grpc.RpcMethodHandler):

    def __init__(self, case):
        self.request_streaming = True
        self.response_streaming = True
        self.request_deserializer = None
        self.response_serializer = None
        self.unary_unary = None
        self.unary_stream = None
        self.stream_unary = None
        self._case = case

    def stream_stream(self, request_iterator, servicer_context):
        _draw_requests(self._case, request_iterator)

        for response in _messages_to_send(self._case.response_count,
                                          self._case.response_size):
            yield response


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self, case):
        self._case = case

    def service(self, handler_call_details):
        return _MethodHandler(self._case)


class _EarlyOkTest(unittest.TestCase):

    def setUp(self):
        self._server = test_common.test_server()
        port = self._server.add_insecure_port('[::]:0')
        self._server.add_generic_rpc_handlers((_GenericHandler(self.case),))
        self._server.start()

        self._channel = grpc.insecure_channel('localhost:%d' % port)
        self._multi_callable = self._channel.stream_stream(_RPC_METHOD)

    def tearDown(self):
        self._server.stop(None)

    def test_early_ok(self):
        requests = _messages_to_send(self.case.request_count,
                                     self.case.request_size)

        response_iterator_call = self._multi_callable(requests)

        _draw_responses(self.case, response_iterator_call)

        self.assertIs(grpc.StatusCode.OK, response_iterator_call.code())


def _cases():
    for request_count in _MessageCount:
        for request_size in _MessageSize:
            for request_reading in _ReadRequests:
                for response_count in _MessageCount:
                    for response_size in _MessageSize:
                        yield _Case(request_count, request_size,
                                    request_reading, response_count,
                                    response_size)


def _test_case_classes():
    for case in _cases():
        yield type(case.create_test_case_name(), (_EarlyOkTest,), {
            'case': case,
            '__module__': _EarlyOkTest.__module__,
        })


def load_tests(loader, tests, pattern):
    return unittest.TestSuite(
        tests=tuple(
            loader.loadTestsFromTestCase(test_case_class)
            for test_case_class in _test_case_classes()))


if __name__ == '__main__':
    unittest.main(verbosity=2)
