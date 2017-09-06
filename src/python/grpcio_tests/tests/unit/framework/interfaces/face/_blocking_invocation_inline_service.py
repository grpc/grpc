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
import itertools
import unittest
from concurrent import futures

import six

# test_interfaces is referenced from specification in this module.
from grpc.framework.foundation import logging_pool
from grpc.framework.interfaces.face import face
from tests.unit.framework.common import test_constants
from tests.unit.framework.common import test_control
from tests.unit.framework.common import test_coverage
from tests.unit.framework.interfaces.face import _3069_test_constant
from tests.unit.framework.interfaces.face import _digest
from tests.unit.framework.interfaces.face import _stock_service
from tests.unit.framework.interfaces.face import test_interfaces  # pylint: disable=unused-import


class TestCase(
        six.with_metaclass(abc.ABCMeta, test_coverage.Coverage,
                           unittest.TestCase)):
    """A test of the Face layer of RPC Framework.

  Concrete subclasses must have an "implementation" attribute of type
  test_interfaces.Implementation and an "invoker_constructor" attribute of type
  _invocation.InvokerConstructor.
  """

    NAME = 'BlockingInvocationInlineServiceTest'

    def setUp(self):
        """See unittest.TestCase.setUp for full specification.

    Overriding implementations must call this implementation.
    """
        self._control = test_control.PauseFailControl()
        self._digest = _digest.digest(_stock_service.STOCK_TEST_SERVICE,
                                      self._control, None)

        generic_stub, dynamic_stubs, self._memo = self.implementation.instantiate(
            self._digest.methods, self._digest.inline_method_implementations,
            None)
        self._invoker = self.invoker_constructor.construct_invoker(
            generic_stub, dynamic_stubs, self._digest.methods)

    def tearDown(self):
        """See unittest.TestCase.tearDown for full specification.

    Overriding implementations must call this implementation.
    """
        self._invoker = None
        self.implementation.destantiate(self._memo)

    def testSuccessfulUnaryRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                response, call = self._invoker.blocking(group, method)(
                    request, test_constants.LONG_TIMEOUT, with_call=True)

                test_messages.verify(request, response, self)

    def testSuccessfulUnaryRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                response_iterator = self._invoker.blocking(group, method)(
                    request, test_constants.LONG_TIMEOUT)
                responses = list(response_iterator)

                test_messages.verify(request, responses, self)

    def testSuccessfulStreamRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                response, call = self._invoker.blocking(group, method)(
                    iter(requests), test_constants.LONG_TIMEOUT, with_call=True)

                test_messages.verify(requests, response, self)

    def testSuccessfulStreamRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                response_iterator = self._invoker.blocking(group, method)(
                    iter(requests), test_constants.LONG_TIMEOUT)
                responses = list(response_iterator)

                test_messages.verify(requests, responses, self)

    def testSequentialInvocations(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                first_request = test_messages.request()
                second_request = test_messages.request()

                first_response = self._invoker.blocking(group, method)(
                    first_request, test_constants.LONG_TIMEOUT)

                test_messages.verify(first_request, first_response, self)

                second_response = self._invoker.blocking(group, method)(
                    second_request, test_constants.LONG_TIMEOUT)

                test_messages.verify(second_request, second_response, self)

    def testParallelInvocations(self):
        pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = []
                response_futures = []
                for _ in range(test_constants.THREAD_CONCURRENCY):
                    request = test_messages.request()
                    response_future = pool.submit(
                        self._invoker.blocking(group, method), request,
                        test_constants.LONG_TIMEOUT)
                    requests.append(request)
                    response_futures.append(response_future)

                responses = [
                    response_future.result()
                    for response_future in response_futures
                ]

                for request, response in zip(requests, responses):
                    test_messages.verify(request, response, self)
        pool.shutdown(wait=True)

    def testWaitingForSomeButNotAllParallelInvocations(self):
        pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = []
                response_futures_to_indices = {}
                for index in range(test_constants.THREAD_CONCURRENCY):
                    request = test_messages.request()
                    response_future = pool.submit(
                        self._invoker.blocking(group, method), request,
                        test_constants.LONG_TIMEOUT)
                    requests.append(request)
                    response_futures_to_indices[response_future] = index

                some_completed_response_futures_iterator = itertools.islice(
                    futures.as_completed(response_futures_to_indices),
                    test_constants.THREAD_CONCURRENCY // 2)
                for response_future in some_completed_response_futures_iterator:
                    index = response_futures_to_indices[response_future]
                    test_messages.verify(requests[index],
                                         response_future.result(), self)
        pool.shutdown(wait=True)

    @unittest.skip('Cancellation impossible with blocking control flow!')
    def testCancelledUnaryRequestUnaryResponse(self):
        raise NotImplementedError()

    @unittest.skip('Cancellation impossible with blocking control flow!')
    def testCancelledUnaryRequestStreamResponse(self):
        raise NotImplementedError()

    @unittest.skip('Cancellation impossible with blocking control flow!')
    def testCancelledStreamRequestUnaryResponse(self):
        raise NotImplementedError()

    @unittest.skip('Cancellation impossible with blocking control flow!')
    def testCancelledStreamRequestStreamResponse(self):
        raise NotImplementedError()

    def testExpiredUnaryRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                with self._control.pause(), self.assertRaises(
                        face.ExpirationError):
                    self._invoker.blocking(group, method)(
                        request, _3069_test_constant.REALLY_SHORT_TIMEOUT)

    def testExpiredUnaryRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                with self._control.pause(), self.assertRaises(
                        face.ExpirationError):
                    response_iterator = self._invoker.blocking(group, method)(
                        request, _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    list(response_iterator)

    def testExpiredStreamRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                with self._control.pause(), self.assertRaises(
                        face.ExpirationError):
                    self._invoker.blocking(group, method)(
                        iter(requests),
                        _3069_test_constant.REALLY_SHORT_TIMEOUT)

    def testExpiredStreamRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                with self._control.pause(), self.assertRaises(
                        face.ExpirationError):
                    response_iterator = self._invoker.blocking(group, method)(
                        iter(requests),
                        _3069_test_constant.REALLY_SHORT_TIMEOUT)
                    list(response_iterator)

    def testFailedUnaryRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                with self._control.fail(), self.assertRaises(face.RemoteError):
                    self._invoker.blocking(group, method)(
                        request, test_constants.LONG_TIMEOUT)

    def testFailedUnaryRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.unary_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                request = test_messages.request()

                with self._control.fail(), self.assertRaises(face.RemoteError):
                    response_iterator = self._invoker.blocking(group, method)(
                        request, test_constants.LONG_TIMEOUT)
                    list(response_iterator)

    def testFailedStreamRequestUnaryResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_unary_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                with self._control.fail(), self.assertRaises(face.RemoteError):
                    self._invoker.blocking(group, method)(
                        iter(requests), test_constants.LONG_TIMEOUT)

    def testFailedStreamRequestStreamResponse(self):
        for (group, method), test_messages_sequence in (
                six.iteritems(self._digest.stream_stream_messages_sequences)):
            for test_messages in test_messages_sequence:
                requests = test_messages.requests()

                with self._control.fail(), self.assertRaises(face.RemoteError):
                    response_iterator = self._invoker.blocking(group, method)(
                        iter(requests), test_constants.LONG_TIMEOUT)
                    list(response_iterator)
