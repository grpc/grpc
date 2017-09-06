# Copyright 2015 gRPC authors.
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
"""Test code for the Face layer of RPC Framework."""

from __future__ import division

import abc
import contextlib
import itertools
import threading
import unittest
from concurrent import futures

import six

# test_interfaces is referenced from specification in this module.
from grpc.framework.foundation import future
from grpc.framework.foundation import logging_pool
from grpc.framework.interfaces.face import face
from tests.unit.framework.common import test_constants
from tests.unit.framework.common import test_control
from tests.unit.framework.common import test_coverage
from tests.unit.framework.interfaces.face import _3069_test_constant
from tests.unit.framework.interfaces.face import _digest
from tests.unit.framework.interfaces.face import _stock_service
from tests.unit.framework.interfaces.face import test_interfaces  # pylint: disable=unused-import


class _PauseableIterator(object):

    def __init__(self, upstream):
        self._upstream = upstream
        self._condition = threading.Condition()
        self._paused = False

    @contextlib.contextmanager
    def pause(self):
        with self._condition:
            self._paused = True
        yield
        with self._condition:
            self._paused = False
            self._condition.notify_all()

    def __iter__(self):
        return self

    def __next__(self):
        return self.next()

    def next(self):
        with self._condition:
            while self._paused:
                self._condition.wait()
        return next(self._upstream)


class _Callback(object):

    def __init__(self):
        self._condition = threading.Condition()
        self._called = False
        self._passed_future = None
        self._passed_other_stuff = None

    def __call__(self, *args, **kwargs):
        with self._condition:
            self._called = True
            if args:
                self._passed_future = args[0]
            if 1 < len(args) or kwargs:
                self._passed_other_stuff = tuple(args[1:]), dict(kwargs)
            self._condition.notify_all()

    def future(self):
        with self._condition:
            while True:
                if self._passed_other_stuff is not None:
                    raise ValueError(
                        'Test callback passed unexpected values: %s',
                        self._passed_other_stuff)
                elif self._called:
                    return self._passed_future
                else:
                    self._condition.wait()


class TestCase(
        six.with_metaclass(abc.ABCMeta, test_coverage.Coverage,
                           unittest.TestCase)):
    """A test of the Face layer of RPC Framework.

  Concrete subclasses must have an "implementation" attribute of type
  test_interfaces.Implementation and an "invoker_constructor" attribute of type
  _invocation.InvokerConstructor.
  """

    NAME = 'FutureInvocationAsynchronousEventServiceTest'

    def setUp(self):
        """See unittest.TestCase.setUp for full specification.

    Overriding implementations must call this implementation.
    """
        self._control = test_control.PauseFailControl()
        self._digest_pool = logging_pool.pool(test_constants.POOL_SIZE)
        self._digest = _digest.digest(_stock_service.STOCK_TEST_SERVICE,
                                      self._control, self._digest_pool)

        generic_stub, dynamic_stubs, self._memo = self.implementation.instantiate(
            self._digest.methods, self._digest.event_method_implementations,
            None)
        self._invoker = self.invoker_constructor.construct_invoker(
            generic_stub, dynamic_stubs, self._digest.methods)

    def tearDown(self):
        """See unittest.TestCase.tearDown for full specification.

    Overriding implementations must call this implementation.
    """
        self._invoker = None
        self.implementation.destantiate(self._memo)
        self._digest_pool.shutdown(wait=True)

    def testSuccessfulUnaryRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()
                callback = _Callback()

                response_future = self._invoker.future(group, method)(
                    request, test_constants.LONG_TIMEOUT)
                response_future.add_done_callback(callback)
                response = response_future.result()

                test_messages.verify(request, response, self)
                self.assertIs(callback.future(), response_future)
                self.assertIsNone(response_future.exception())
                self.assertIsNone(response_future.traceback())

    def testSuccessfulUnaryRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                response_iterator = self._invoker.future(group, method)(
                    request, test_constants.LONG_TIMEOUT)
                responses = list(response_iterator)

                test_messages.verify(request, responses, self)

    def testSuccessfulStreamRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()
                request_iterator = _PauseableIterator(iter(requests))
                callback = _Callback()

                # Use of a paused iterator of requests allows us to test that control is
                # returned to calling code before the iterator yields any requests.
                with request_iterator.pause():
                    response_future = self._invoker.future(group, method)(
                        request_iterator, test_constants.LONG_TIMEOUT)
                    response_future.add_done_callback(callback)
                future_passed_to_callback = callback.future()
                response = future_passed_to_callback.result()

                test_messages.verify(requests, response, self)
                self.assertIs(future_passed_to_callback, response_future)
                self.assertIsNone(response_future.exception())
                self.assertIsNone(response_future.traceback())

    def testSuccessfulStreamRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()
                request_iterator = _PauseableIterator(iter(requests))

                # Use of a paused iterator of requests allows us to test that control is
                # returned to calling code before the iterator yields any requests.
                with request_iterator.pause():
                    response_iterator = self._invoker.future(group, method)(
                        request_iterator, test_constants.LONG_TIMEOUT)
                responses = list(response_iterator)

                test_messages.verify(requests, responses, self)

    def testSequentialInvocations(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                first_request = test_messages.request()
                second_request = test_messages.request()

                first_response_future = self._invoker.future(group, method)(
                    first_request, test_constants.LONG_TIMEOUT)
                first_response = first_response_future.result()

                test_messages.verify(first_request, first_response, self)

                second_response_future = self._invoker.future(group, method)(
                    second_request, test_constants.LONG_TIMEOUT)
                second_response = second_response_future.result()

                test_messages.verify(second_request, second_response, self)

    def testParallelInvocations(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                first_request = test_messages.request()
                second_request = test_messages.request()

                first_response_future = self._invoker.future(group, method)(
                    first_request, test_constants.LONG_TIMEOUT)
                second_response_future = self._invoker.future(group, method)(
                    second_request, test_constants.LONG_TIMEOUT)
                first_response = first_response_future.result()
                second_response = second_response_future.result()

                test_messages.verify(first_request, first_response, self)
                test_messages.verify(second_request, second_response, self)

        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = []
                response_futures = []
                for _ in range(test_constants.THREAD_CONCURRENCY):
                    request = test_messages.request()
                    response_future = self._invoker.future(group, method)(
                        request, test_constants.LONG_TIMEOUT)
                    requests.append(request)
                    response_futures.append(response_future)

                responses = [
                    response_future.result()
                    for response_future in response_futures
                ]

                for request, response in zip(requests, responses):
                    test_messages.verify(request, response, self)

    def testWaitingForSomeButNotAllParallelInvocations(self):
        pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = []
                response_futures_to_indices = {}
                for index in range(test_constants.THREAD_CONCURRENCY):
                    request = test_messages.request()
                    inner_response_future = self._invoker.future(group, method)(
                        request, test_constants.LONG_TIMEOUT)
                    outer_response_future = pool.submit(
                        inner_response_future.result)
                    requests.append(request)
                    response_futures_to_indices[outer_response_future] = index

                some_completed_response_futures_iterator = itertools.islice(
                    futures.as_completed(response_futures_to_indices),
                    test_constants.THREAD_CONCURRENCY // 2)
                for response_future in some_completed_response_futures_iterator:
                    index = response_futures_to_indices[response_future]
                    test_messages.verify(requests[index],
                                         response_future.result(), self)
        pool.shutdown(wait=True)

    def testCancelledUnaryRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()
                callback = _Callback()

                with self._control.pause():
                    response_future = self._invoker.future(group, method)(
                        request, test_constants.LONG_TIMEOUT)
                    response_future.add_done_callback(callback)
                    cancel_method_return_value = response_future.cancel()

                self.assertIs(callback.future(), response_future)
                self.assertFalse(cancel_method_return_value)
                self.assertTrue(response_future.cancelled())
                with self.assertRaises(future.CancelledError):
                    response_future.result()
                with self.assertRaises(future.CancelledError):
                    response_future.exception()
                with self.assertRaises(future.CancelledError):
                    response_future.traceback()

    def testCancelledUnaryRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                with self._control.pause():
                    response_iterator = self._invoker.future(group, method)(
                        request, test_constants.LONG_TIMEOUT)
                    response_iterator.cancel()

                with self.assertRaises(face.CancellationError):
                    next(response_iterator)

    def testCancelledStreamRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()
                callback = _Callback()

                with self._control.pause():
                    response_future = self._invoker.future(group, method)(
                        iter(requests), test_constants.LONG_TIMEOUT)
                    response_future.add_done_callback(callback)
                    cancel_method_return_value = response_future.cancel()

                self.assertIs(callback.future(), response_future)
                self.assertFalse(cancel_method_return_value)
                self.assertTrue(response_future.cancelled())
                with self.assertRaises(future.CancelledError):
                    response_future.result()
                with self.assertRaises(future.CancelledError):
                    response_future.exception()
                with self.assertRaises(future.CancelledError):
                    response_future.traceback()

    def testCancelledStreamRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                with self._control.pause():
                    response_iterator = self._invoker.future(group, method)(
                        iter(requests), test_constants.LONG_TIMEOUT)
                    response_iterator.cancel()

                with self.assertRaises(face.CancellationError):
                    next(response_iterator)

    def testExpiredUnaryRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()
                callback = _Callback()

                with self._control.pause():
                    response_future = self._invoker.future(group, method)(
                        request, _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    response_future.add_done_callback(callback)
                    self.assertIs(callback.future(), response_future)
                    self.assertIsInstance(response_future.exception(),
                                          face.ExpirationError)
                    with self.assertRaises(face.ExpirationError):
                        response_future.result()
                    self.assertIsInstance(response_future.exception(),
                                          face.AbortionError)
                    self.assertIsNotNone(response_future.traceback())

    def testExpiredUnaryRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                with self._control.pause():
                    response_iterator = self._invoker.future(group, method)(
                        request, _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    with self.assertRaises(face.ExpirationError):
                        list(response_iterator)

    def testExpiredStreamRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()
                callback = _Callback()

                with self._control.pause():
                    response_future = self._invoker.future(group, method)(
                        iter(requests),
                        _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    response_future.add_done_callback(callback)
                    self.assertIs(callback.future(), response_future)
                    self.assertIsInstance(response_future.exception(),
                                          face.ExpirationError)
                    with self.assertRaises(face.ExpirationError):
                        response_future.result()
                    self.assertIsInstance(response_future.exception(),
                                          face.AbortionError)
                    self.assertIsNotNone(response_future.traceback())

    def testExpiredStreamRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                with self._control.pause():
                    response_iterator = self._invoker.future(group, method)(
                        iter(requests),
                        _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    with self.assertRaises(face.ExpirationError):
                        list(response_iterator)

    def testFailedUnaryRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()
                callback = _Callback()
                abortion_callback = _Callback()

                with self._control.fail():
                    response_future = self._invoker.future(group, method)(
                        request, _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    response_future.add_done_callback(callback)
                    response_future.add_abortion_callback(abortion_callback)

                    self.assertIs(callback.future(), response_future)
                    # Because the servicer fails outside of the thread from which the
                    # servicer-side runtime called into it its failure is
                    # indistinguishable from simply not having called its
                    # response_callback before the expiration of the RPC.
                    self.assertIsInstance(response_future.exception(),
                                          face.ExpirationError)
                    with self.assertRaises(face.ExpirationError):
                        response_future.result()
                    self.assertIsNotNone(response_future.traceback())
                    self.assertIsNotNone(abortion_callback.future())

    def testFailedUnaryRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                # Because the servicer fails outside of the thread from which the
                # servicer-side runtime called into it its failure is indistinguishable
                # from simply not having called its response_consumer before the
                # expiration of the RPC.
                with self._control.fail(), self.assertRaises(
                        face.ExpirationError):
                    response_iterator = self._invoker.future(group, method)(
                        request, _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    list(response_iterator)

    def testFailedStreamRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()
                callback = _Callback()
                abortion_callback = _Callback()

                with self._control.fail():
                    response_future = self._invoker.future(group, method)(
                        iter(requests),
                        _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    response_future.add_done_callback(callback)
                    response_future.add_abortion_callback(abortion_callback)

                    self.assertIs(callback.future(), response_future)
                    # Because the servicer fails outside of the thread from which the
                    # servicer-side runtime called into it its failure is
                    # indistinguishable from simply not having called its
                    # response_callback before the expiration of the RPC.
                    self.assertIsInstance(response_future.exception(),
                                          face.ExpirationError)
                    with self.assertRaises(face.ExpirationError):
                        response_future.result()
                    self.assertIsNotNone(response_future.traceback())
                    self.assertIsNotNone(abortion_callback.future())

    def testFailedStreamRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                # Because the servicer fails outside of the thread from which the
                # servicer-side runtime called into it its failure is indistinguishable
                # from simply not having called its response_consumer before the
                # expiration of the RPC.
                with self._control.fail(), self.assertRaises(
                        face.ExpirationError):
                    response_iterator = self._invoker.future(group, method)(
                        iter(requests),
                        _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    list(response_iterator)
