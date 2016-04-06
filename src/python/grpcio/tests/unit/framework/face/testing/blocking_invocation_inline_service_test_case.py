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

# unittest is referenced from specification in this module.
import abc
import unittest  # pylint: disable=unused-import

import six

from grpc.framework.face import exceptions
from tests.unit.framework.common import test_constants
from tests.unit.framework.face.testing import control
from tests.unit.framework.face.testing import coverage
from tests.unit.framework.face.testing import digest
from tests.unit.framework.face.testing import stock_service
from tests.unit.framework.face.testing import test_case


class BlockingInvocationInlineServiceTestCase(
    six.with_metaclass(abc.ABCMeta,
    test_case.FaceTestCase, coverage.BlockingCoverage)):
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
        self.digest.inline_method_implementations, None)

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

        response = self.stub.blocking_value_in_value_out(
            name, request, test_constants.LONG_TIMEOUT)

        test_messages.verify(request, response, self)

  def testSuccessfulUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        response_iterator = self.stub.inline_value_in_stream_out(
            name, request, test_constants.LONG_TIMEOUT)
        responses = list(response_iterator)

        test_messages.verify(request, responses, self)

  def testSuccessfulStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        response = self.stub.blocking_stream_in_value_out(
            name, iter(requests), test_constants.LONG_TIMEOUT)

        test_messages.verify(requests, response, self)

  def testSuccessfulStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        response_iterator = self.stub.inline_stream_in_stream_out(
            name, iter(requests), test_constants.LONG_TIMEOUT)
        responses = list(response_iterator)

        test_messages.verify(requests, responses, self)

  def testSequentialInvocations(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        first_request = test_messages.request()
        second_request = test_messages.request()

        first_response = self.stub.blocking_value_in_value_out(
            name, first_request, test_constants.SHORT_TIMEOUT)

        test_messages.verify(first_request, first_response, self)

        second_response = self.stub.blocking_value_in_value_out(
            name, second_request, test_constants.SHORT_TIMEOUT)

        test_messages.verify(second_request, second_response, self)

  def testExpiredUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.pause(), self.assertRaises(
            exceptions.ExpirationError):
          multi_callable = self.stub.unary_unary_multi_callable(name)
          multi_callable(request, test_constants.SHORT_TIMEOUT)

  def testExpiredUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.pause(), self.assertRaises(
            exceptions.ExpirationError):
          response_iterator = self.stub.inline_value_in_stream_out(
              name, request, test_constants.SHORT_TIMEOUT)
          list(response_iterator)

  def testExpiredStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.pause(), self.assertRaises(
            exceptions.ExpirationError):
          multi_callable = self.stub.stream_unary_multi_callable(name)
          multi_callable(iter(requests), test_constants.SHORT_TIMEOUT)

  def testExpiredStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.pause(), self.assertRaises(
            exceptions.ExpirationError):
          response_iterator = self.stub.inline_stream_in_stream_out(
              name, iter(requests), test_constants.SHORT_TIMEOUT)
          list(response_iterator)

  def testFailedUnaryRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.fail(), self.assertRaises(exceptions.ServicerError):
          self.stub.blocking_value_in_value_out(name, request,
                                                test_constants.SHORT_TIMEOUT)

  def testFailedUnaryRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.unary_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        request = test_messages.request()

        with self.control.fail(), self.assertRaises(exceptions.ServicerError):
          response_iterator = self.stub.inline_value_in_stream_out(
              name, request, test_constants.SHORT_TIMEOUT)
          list(response_iterator)

  def testFailedStreamRequestUnaryResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_unary_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.fail(), self.assertRaises(exceptions.ServicerError):
          self.stub.blocking_stream_in_value_out(name, iter(requests),
                                                 test_constants.SHORT_TIMEOUT)

  def testFailedStreamRequestStreamResponse(self):
    for name, test_messages_sequence in (
        six.iteritems(self.digest.stream_stream_messages_sequences)):
      for test_messages in test_messages_sequence:
        requests = test_messages.requests()

        with self.control.fail(), self.assertRaises(exceptions.ServicerError):
          response_iterator = self.stub.inline_stream_in_stream_out(
              name, iter(requests), test_constants.SHORT_TIMEOUT)
          list(response_iterator)
