# Copyright 2015, Google Inc.
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

"""A test to verify an implementation of the Face layer of RPC Framework."""

import abc
import contextlib
import threading
import unittest

import six

from grpc.framework.face import exceptions
from grpc.framework.foundation import future
from grpc.framework.foundation import logging_pool
from tests.unit.framework.common import test_constants
from tests.unit.framework.face.testing import control
from tests.unit.framework.face.testing import coverage
from tests.unit.framework.face.testing import digest
from tests.unit.framework.face.testing import stock_service
from tests.unit.framework.face.testing import test_case

_MAXIMUM_POOL_SIZE = 10


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


class FutureInvocationAsynchronousEventServiceTestCase(
    six.with_metaclass(abc.ABCMeta,
    test_case.FaceTestCase, coverage.FullCoverage)):
  """A test of the Face layer of RPC Framework.

  Concrete subclasses must also extend unittest.TestCase.
  """

  def setUp(self):
    """See unittest.TestCase.setUp for full specification.

    Overriding implementations must call this implementation.
    """
    self.control = control.PauseFailControl()
    self.digest_pool = logging_pool.pool(_MAXIMUM_POOL_SIZE)
    self.digest = digest.digest(
        stock_service.STOCK_TEST_SERVICE, self.control, self.digest_pool)

    self.stub, self.memo = self.set_up_implementation(
        self.digest.name, self.digest.methods,
        self.digest.event_method_implementations, None)

  def tearDown(self):
    """See unittest.TestCase.tearDown for full specification.

    Overriding implementations must call this implementation.
    """
    self.tear_down_implementation(self.memo)
    self.digest_pool.shutdown(wait=True)

  def testSuccessfulUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        response_future = self.stub.future_value_in_value_out(
            name, request, test_constants.SHORT_TIMEOUT)
        response = response_future.result()

        test_messages.verify(request, response, self)

  def testSuccessfulUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        response_iterator = self.stub.inline_value_in_stream_out(
            name, request, test_constants.SHORT_TIMEOUT)
        responses = list(response_iterator)

        test_messages.verify(request, responses, self)

  def testSuccessfulStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()
        request_iterator = _PauseableIterator(iter(requests))

        # Use of a paused iterator of requests allows us to test that control is
        # returned to calling code before the iterator yields any requests.
        with request_iterator.pause():
          response_future = self.stub.future_stream_in_value_out(
              name, request_iterator, test_constants.SHORT_TIMEOUT)
        response = response_future.result()

        test_messages.verify(requests, response, self)

  def testSuccessfulStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()
        request_iterator = _PauseableIterator(iter(requests))

        # Use of a paused iterator of requests allows us to test that control is
        # returned to calling code before the iterator yields any requests.
        with request_iterator.pause():
          response_iterator = self.stub.inline_stream_in_stream_out(
              name, request_iterator, test_constants.SHORT_TIMEOUT)
        responses = list(response_iterator)

        test_messages.verify(requests, responses, self)

  def testSequentialInvocations(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        first_request = test_messages.request()
        second_request = test_messages.request()

        first_response_future = self.stub.future_value_in_value_out(
            name, first_request, test_constants.SHORT_TIMEOUT)
        first_response = first_response_future.result()

        test_messages.verify(first_request, first_response, self)

        second_response_future = self.stub.future_value_in_value_out(
            name, second_request, test_constants.SHORT_TIMEOUT)
        second_response = second_response_future.result()

        test_messages.verify(second_request, second_response, self)

  def testExpiredUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.pause():
          multi_callable = self.stub.unary_unary_multi_callable(name)
          response_future = multi_callable.future(request,
                                                  test_constants.SHORT_TIMEOUT)
          self.assertIsInstance(
              response_future.exception(), exceptions.ExpirationError)
          with self.assertRaises(exceptions.ExpirationError):
            response_future.result()

  def testExpiredUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.pause():
          response_iterator = self.stub.inline_value_in_stream_out(
              name, request, test_constants.SHORT_TIMEOUT)
          with self.assertRaises(exceptions.ExpirationError):
            list(response_iterator)

  def testExpiredStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.pause():
          multi_callable = self.stub.stream_unary_multi_callable(name)
          response_future = multi_callable.future(iter(requests),
                                                  test_constants.SHORT_TIMEOUT)
          self.assertIsInstance(
              response_future.exception(), exceptions.ExpirationError)
          with self.assertRaises(exceptions.ExpirationError):
            response_future.result()

  def testExpiredStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.pause():
          response_iterator = self.stub.inline_stream_in_stream_out(
              name, iter(requests), test_constants.SHORT_TIMEOUT)
          with self.assertRaises(exceptions.ExpirationError):
            list(response_iterator)

  def testFailedUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.fail():
          response_future = self.stub.future_value_in_value_out(
              name, request, test_constants.SHORT_TIMEOUT)

          # Because the servicer fails outside of the thread from which the
          # servicer-side runtime called into it its failure is
          # indistinguishable from simply not having called its
          # response_callback before the expiration of the RPC.
          self.assertIsInstance(
              response_future.exception(), exceptions.ExpirationError)
          with self.assertRaises(exceptions.ExpirationError):
            response_future.result()

  def testFailedUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        # Because the servicer fails outside of the thread from which the
        # servicer-side runtime called into it its failure is indistinguishable
        # from simply not having called its response_consumer before the
        # expiration of the RPC.
        with self.control.fail(), self.assertRaises(exceptions.ExpirationError):
          response_iterator = self.stub.inline_value_in_stream_out(
              name, request, test_constants.SHORT_TIMEOUT)
          list(response_iterator)

  def testFailedStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.fail():
          response_future = self.stub.future_stream_in_value_out(
              name, iter(requests), test_constants.SHORT_TIMEOUT)

          # Because the servicer fails outside of the thread from which the
          # servicer-side runtime called into it its failure is
          # indistinguishable from simply not having called its
          # response_callback before the expiration of the RPC.
          self.assertIsInstance(
              response_future.exception(), exceptions.ExpirationError)
          with self.assertRaises(exceptions.ExpirationError):
            response_future.result()

  def testFailedStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        # Because the servicer fails outside of the thread from which the
        # servicer-side runtime called into it its failure is indistinguishable
        # from simply not having called its response_consumer before the
        # expiration of the RPC.
        with self.control.fail(), self.assertRaises(exceptions.ExpirationError):
          response_iterator = self.stub.inline_stream_in_stream_out(
              name, iter(requests), test_constants.SHORT_TIMEOUT)
          list(response_iterator)

  def testParallelInvocations(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        first_request = test_messages.request()
        second_request = test_messages.request()

        # TODO(bug 2039): use LONG_TIMEOUT instead
        first_response_future = self.stub.future_value_in_value_out(
            name, first_request, test_constants.SHORT_TIMEOUT)
        second_response_future = self.stub.future_value_in_value_out(
            name, second_request, test_constants.SHORT_TIMEOUT)
        first_response = first_response_future.result()
        second_response = second_response_future.result()

        test_messages.verify(first_request, first_response, self)
        test_messages.verify(second_request, second_response, self)

  @unittest.skip('TODO(nathaniel): implement.')
  def testWaitingForSomeButNotAllParallelInvocations(self):
    raise NotImplementedError()

  def testCancelledUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.pause():
          response_future = self.stub.future_value_in_value_out(
              name, request, test_constants.SHORT_TIMEOUT)
          cancel_method_return_value = response_future.cancel()

        self.assertFalse(cancel_method_return_value)
        self.assertTrue(response_future.cancelled())

  def testCancelledUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.pause():
          response_iterator = self.stub.inline_value_in_stream_out(
              name, request, test_constants.SHORT_TIMEOUT)
          response_iterator.cancel()

        with self.assertRaises(future.CancelledError):
          next(response_iterator)

  def testCancelledStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.pause():
          response_future = self.stub.future_stream_in_value_out(
              name, iter(requests), test_constants.SHORT_TIMEOUT)
          cancel_method_return_value = response_future.cancel()

        self.assertFalse(cancel_method_return_value)
        self.assertTrue(response_future.cancelled())

  def testCancelledStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.pause():
          response_iterator = self.stub.inline_stream_in_stream_out(
              name, iter(requests), test_constants.SHORT_TIMEOUT)
          response_iterator.cancel()

        with self.assertRaises(future.CancelledError):
          next(response_iterator)
