# Copyright 2026 gRPC authors.
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
"""Tests for the blocking unary-response paths of the sync client.

Covers `__call__` and `with_call` of unary-unary and stream-unary
multi-callables, and the `_end_unary_response_blocking` /
`_end_unary_response_blocking_with_call` helpers in `grpc._channel`.
"""

import logging
import time
import unittest
from unittest import mock

import grpc
from grpc import _channel

from tests.unit._rpc_test_helpers import BaseRPCTest
from tests.unit._rpc_test_helpers import stream_unary_multi_callable
from tests.unit._rpc_test_helpers import unary_unary_multi_callable
from tests.unit.framework.common import test_constants

_RESPONSE = b"\x01\x02"
_DETAILS = "test details"
_TRAILING_METADATA_KEY = "testkey"


def _completed_state(code, details=None):
    state = _channel._RPCState((), None, None, code, details)
    state.response = _RESPONSE
    return state


class EndUnaryResponseBlockingHelpersTest(unittest.TestCase):
    """Directly exercises the two helpers with a completed _RPCState."""

    def test_ok_returns_bare_response(self):
        state = _completed_state(grpc.StatusCode.OK)

        response = _channel._end_unary_response_blocking(state)

        self.assertIs(_RESPONSE, response)

    def test_non_ok_raises_rpc_error_that_is_a_call(self):
        state = _completed_state(grpc.StatusCode.UNAVAILABLE, _DETAILS)

        with self.assertRaises(grpc.RpcError) as exception_context:
            _channel._end_unary_response_blocking(state)

        error = exception_context.exception
        self.assertIsInstance(error, grpc.Call)
        self.assertIs(grpc.StatusCode.UNAVAILABLE, error.code())
        self.assertEqual(_DETAILS, error.details())

    def test_with_call_ok_returns_response_and_call(self):
        state = _completed_state(grpc.StatusCode.OK)
        # The rendezvous only stores the call object; since the state is
        # already completed it never touches it (not even in __del__).
        fake_call = object()

        result = _channel._end_unary_response_blocking_with_call(
            state, fake_call, None
        )

        self.assertIsInstance(result, tuple)
        self.assertEqual(2, len(result))
        response, call = result
        self.assertIs(_RESPONSE, response)
        self.assertIsInstance(call, grpc.Call)
        self.assertIs(grpc.StatusCode.OK, call.code())

    def test_with_call_without_deadline_has_no_time_remaining(self):
        state = _completed_state(grpc.StatusCode.OK)

        _, call = _channel._end_unary_response_blocking_with_call(
            state, object(), None
        )

        self.assertIsNone(call.time_remaining())

    def test_with_call_forwards_deadline_to_call(self):
        state = _completed_state(grpc.StatusCode.OK)
        timeout = 1000.0
        deadline = time.time() + timeout

        _, call = _channel._end_unary_response_blocking_with_call(
            state, object(), deadline
        )

        time_remaining = call.time_remaining()
        self.assertIsNotNone(time_remaining)
        self.assertGreater(time_remaining, 0)
        self.assertLessEqual(time_remaining, timeout)

    def test_with_call_non_ok_raises_without_building_call(self):
        state = _completed_state(grpc.StatusCode.UNAVAILABLE, _DETAILS)

        with mock.patch.object(
            _channel, "_MultiThreadedRendezvous"
        ) as rendezvous_class:
            with self.assertRaises(grpc.RpcError) as exception_context:
                _channel._end_unary_response_blocking_with_call(
                    state, object(), None
                )

        rendezvous_class.assert_not_called()
        self.assertIs(
            grpc.StatusCode.UNAVAILABLE, exception_context.exception.code()
        )


class BlockingUnaryResponseRPCTest(BaseRPCTest, unittest.TestCase):
    """End-to-end tests of the four call sites that use the helpers.

    Complements _rpc_part_1_test/_rpc_part_2_test, which do not cover a
    failing unary-unary `__call__`, a failing stream-unary `with_call`, or
    the exact shape of the returned values.
    """

    def _stream_requests(self):
        return iter(
            tuple(b"\x07\x08" for _ in range(test_constants.STREAM_LENGTH))
        )

    def _assert_returned_call(self, call):
        self.assertIsInstance(call, grpc.Call)
        self.assertIs(grpc.StatusCode.OK, call.code())
        self.assertIn(
            _TRAILING_METADATA_KEY,
            dict(call.trailing_metadata() or ()),
        )

    def testUnaryUnaryCallReturnsBareResponse(self):
        request = b"\x07\x08"
        expected_response = self._handler.handle_unary_unary(request, None)

        response = unary_unary_multi_callable(self._channel)(request)

        self.assertNotIsInstance(response, tuple)
        self.assertEqual(expected_response, response)

    def testUnaryUnaryWithCallReturnsResponseAndCall(self):
        request = b"\x07\x08"
        expected_response = self._handler.handle_unary_unary(request, None)

        result = unary_unary_multi_callable(self._channel).with_call(request)

        self.assertIsInstance(result, tuple)
        self.assertEqual(2, len(result))
        response, call = result
        self.assertEqual(expected_response, response)
        self._assert_returned_call(call)

    def testStreamUnaryCallReturnsBareResponse(self):
        expected_response = self._handler.handle_stream_unary(
            self._stream_requests(), None
        )

        response = stream_unary_multi_callable(self._channel)(
            self._stream_requests()
        )

        self.assertNotIsInstance(response, tuple)
        self.assertEqual(expected_response, response)

    def testStreamUnaryWithCallReturnsResponseAndCall(self):
        expected_response = self._handler.handle_stream_unary(
            self._stream_requests(), None
        )

        result = stream_unary_multi_callable(self._channel).with_call(
            self._stream_requests()
        )

        self.assertIsInstance(result, tuple)
        self.assertEqual(2, len(result))
        response, call = result
        self.assertEqual(expected_response, response)
        self._assert_returned_call(call)

    def testFailedUnaryUnaryCallRaises(self):
        multi_callable = unary_unary_multi_callable(self._channel)

        with self._control.fail():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable(b"\x07\x08")

        self.assertIsInstance(exception_context.exception, grpc.Call)
        self.assertIs(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    def testFailedStreamUnaryWithCallRaises(self):
        multi_callable = stream_unary_multi_callable(self._channel)

        with self._control.fail():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable.with_call(self._stream_requests())

        self.assertIsInstance(exception_context.exception, grpc.Call)
        self.assertIs(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    # TODO(janiewicz): The call sites pass
    # deadline=None (regression from ca7ba4d0ac), so time_remaining() is None
    # even when a timeout is set. Flip these to assertIsNotNone once the
    # deadline is propagated.
    def testUnaryUnaryWithCallTimeRemainingIsNone(self):
        _, call = unary_unary_multi_callable(self._channel).with_call(
            b"\x07\x08", timeout=test_constants.LONG_TIMEOUT
        )

        self.assertIsNone(call.time_remaining())

    def testStreamUnaryWithCallTimeRemainingIsNone(self):
        _, call = stream_unary_multi_callable(self._channel).with_call(
            self._stream_requests(), timeout=test_constants.LONG_TIMEOUT
        )

        self.assertIsNone(call.time_remaining())


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
