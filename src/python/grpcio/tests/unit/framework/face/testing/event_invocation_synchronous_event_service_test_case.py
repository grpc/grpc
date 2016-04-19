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
import unittest

import six

from grpc.framework.face import interfaces
from tests.unit.framework.common import test_constants
from tests.unit.framework.face.testing import callback as testing_callback
from tests.unit.framework.face.testing import control
from tests.unit.framework.face.testing import coverage
from tests.unit.framework.face.testing import digest
from tests.unit.framework.face.testing import stock_service
from tests.unit.framework.face.testing import test_case


class EventInvocationSynchronousEventServiceTestCase(
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
    self.digest = digest.digest(
        stock_service.STOCK_TEST_SERVICE, self.control, None)

    self.stub, self.memo = self.set_up_implementation(
        self.digest.name, self.digest.methods,
        self.digest.event_method_implementations, None)

  def tearDown(self):
    """See unittest.TestCase.tearDown for full specification.

    Overriding implementations must call this implementation.
    """
    self.tear_down_implementation(self.memo)

  def testSuccessfulUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()
        callback = testing_callback.Callback()

        self.stub.event_value_in_value_out(
            name, request, callback.complete, callback.abort,
            test_constants.SHORT_TIMEOUT)
        callback.block_until_terminated()
        response = callback.response()

        test_messages.verify(request, response, self)

  def testSuccessfulUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()
        callback = testing_callback.Callback()

        self.stub.event_value_in_stream_out(
            name, request, callback, callback.abort,
            test_constants.SHORT_TIMEOUT)
        callback.block_until_terminated()
        responses = callback.responses()

        test_messages.verify(request, responses, self)

  def testSuccessfulStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()
        callback = testing_callback.Callback()

        unused_call, request_consumer = self.stub.event_stream_in_value_out(
            name, callback.complete, callback.abort,
            test_constants.SHORT_TIMEOUT)
        for request in requests:
          request_consumer.consume(request)
        request_consumer.terminate()
        callback.block_until_terminated()
        response = callback.response()

        test_messages.verify(requests, response, self)

  def testSuccessfulStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()
        callback = testing_callback.Callback()

        unused_call, request_consumer = self.stub.event_stream_in_stream_out(
            name, callback, callback.abort, test_constants.SHORT_TIMEOUT)
        for request in requests:
          request_consumer.consume(request)
        request_consumer.terminate()
        callback.block_until_terminated()
        responses = callback.responses()

        test_messages.verify(requests, responses, self)

  def testSequentialInvocations(self):
    # pylint: disable=cell-var-from-loop
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        first_request = test_messages.request()
        second_request = test_messages.request()
        first_callback = testing_callback.Callback()
        second_callback = testing_callback.Callback()

        def make_second_invocation(first_response):
          first_callback.complete(first_response)
          self.stub.event_value_in_value_out(
              name, second_request, second_callback.complete,
              second_callback.abort, test_constants.SHORT_TIMEOUT)

        self.stub.event_value_in_value_out(
            name, first_request, make_second_invocation, first_callback.abort,
           test_constants.SHORT_TIMEOUT)
        second_callback.block_until_terminated()

        first_response = first_callback.response()
        second_response = second_callback.response()
        test_messages.verify(first_request, first_response, self)
        test_messages.verify(second_request, second_response, self)

  def testExpiredUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()
        callback = testing_callback.Callback()

        with self.control.pause():
          self.stub.event_value_in_value_out(
              name, request, callback.complete, callback.abort,
              test_constants.SHORT_TIMEOUT)
          callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.EXPIRED, callback.abortion())

  def testExpiredUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()
        callback = testing_callback.Callback()

        with self.control.pause():
          self.stub.event_value_in_stream_out(
              name, request, callback, callback.abort,
              test_constants.SHORT_TIMEOUT)
          callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.EXPIRED, callback.abortion())

  def testExpiredStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for unused_test_messages in test_messages_sequence:
        callback = testing_callback.Callback()

        self.stub.event_stream_in_value_out(
            name, callback.complete, callback.abort,
            test_constants.SHORT_TIMEOUT)
        callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.EXPIRED, callback.abortion())

  def testExpiredStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()
        callback = testing_callback.Callback()

        unused_call, request_consumer = self.stub.event_stream_in_stream_out(
            name, callback, callback.abort, test_constants.SHORT_TIMEOUT)
        for request in requests:
          request_consumer.consume(request)
        callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.EXPIRED, callback.abortion())

  def testFailedUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()
        callback = testing_callback.Callback()

        with self.control.fail():
          self.stub.event_value_in_value_out(
              name, request, callback.complete, callback.abort,
              test_constants.SHORT_TIMEOUT)
          callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.SERVICER_FAILURE,
                         callback.abortion())

  def testFailedUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()
        callback = testing_callback.Callback()

        with self.control.fail():
          self.stub.event_value_in_stream_out(
              name, request, callback, callback.abort,
              test_constants.SHORT_TIMEOUT)
          callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.SERVICER_FAILURE,
                         callback.abortion())

  def testFailedStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()
        callback = testing_callback.Callback()

        with self.control.fail():
          unused_call, request_consumer = self.stub.event_stream_in_value_out(
              name, callback.complete, callback.abort,
              test_constants.SHORT_TIMEOUT)
          for request in requests:
            request_consumer.consume(request)
          request_consumer.terminate()
          callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.SERVICER_FAILURE,
                         callback.abortion())

  def testFailedStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()
        callback = testing_callback.Callback()

        with self.control.fail():
          unused_call, request_consumer = self.stub.event_stream_in_stream_out(
              name, callback, callback.abort, test_constants.SHORT_TIMEOUT)
          for request in requests:
            request_consumer.consume(request)
          request_consumer.terminate()
          callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.SERVICER_FAILURE, callback.abortion())

  def testParallelInvocations(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        first_request = test_messages.request()
        first_callback = testing_callback.Callback()
        second_request = test_messages.request()
        second_callback = testing_callback.Callback()

        self.stub.event_value_in_value_out(
            name, first_request, first_callback.complete, first_callback.abort,
           test_constants.SHORT_TIMEOUT)
        self.stub.event_value_in_value_out(
            name, second_request, second_callback.complete,
            second_callback.abort, test_constants.SHORT_TIMEOUT)
        first_callback.block_until_terminated()
        second_callback.block_until_terminated()

        first_response = first_callback.response()
        second_response = second_callback.response()
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
        callback = testing_callback.Callback()

        with self.control.pause():
          call = self.stub.event_value_in_value_out(
              name, request, callback.complete, callback.abort,
              test_constants.SHORT_TIMEOUT)
          call.cancel()
          callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.CANCELLED, callback.abortion())

  def testCancelledUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()
        callback = testing_callback.Callback()

        call = self.stub.event_value_in_stream_out(
            name, request, callback, callback.abort,
            test_constants.SHORT_TIMEOUT)
        call.cancel()
        callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.CANCELLED, callback.abortion())

  def testCancelledStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()
        callback = testing_callback.Callback()

        call, request_consumer = self.stub.event_stream_in_value_out(
            name, callback.complete, callback.abort,
            test_constants.SHORT_TIMEOUT)
        for request in requests:
          request_consumer.consume(request)
        call.cancel()
        callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.CANCELLED, callback.abortion())

  def testCancelledStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for unused_test_messages in test_messages_sequence:
        callback = testing_callback.Callback()

        call, unused_request_consumer = self.stub.event_stream_in_stream_out(
            name, callback, callback.abort, test_constants.SHORT_TIMEOUT)
        call.cancel()
        callback.block_until_terminated()

        self.assertEqual(interfaces.Abortion.CANCELLED, callback.abortion())
