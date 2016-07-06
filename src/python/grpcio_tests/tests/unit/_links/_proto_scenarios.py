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

"""Test scenarios using protocol buffers."""

import abc
import threading

import six

from tests.unit._junkdrawer import math_pb2
from tests.unit.framework.common import test_constants


class ProtoScenario(six.with_metaclass(abc.ABCMeta)):
  """An RPC test scenario using protocol buffers."""

  @abc.abstractmethod
  def group_and_method(self):
    """Access the test group and method.

    Returns:
      The test group and method as a pair.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def serialize_request(self, request):
    """Serialize a request protocol buffer.

    Args:
      request: A request protocol buffer.

    Returns:
      The bytestring serialization of the given request protocol buffer.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def deserialize_request(self, request_bytestring):
    """Deserialize a request protocol buffer.

    Args:
      request_bytestring: The bytestring serialization of a request protocol
        buffer.

    Returns:
      The request protocol buffer deserialized from the given byte string.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def serialize_response(self, response):
    """Serialize a response protocol buffer.

    Args:
      response: A response protocol buffer.

    Returns:
      The bytestring serialization of the given response protocol buffer.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def deserialize_response(self, response_bytestring):
    """Deserialize a response protocol buffer.

    Args:
      response_bytestring: The bytestring serialization of a response protocol
        buffer.

    Returns:
      The response protocol buffer deserialized from the given byte string.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def requests(self):
    """Access the sequence of requests for this scenario.

    Returns:
      A sequence of request protocol buffers.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def response_for_request(self, request):
    """Access the response for a particular request.

    Args:
      request: A request protocol buffer.

    Returns:
      The response protocol buffer appropriate for the given request.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def verify_requests(self, experimental_requests):
    """Verify the requests transmitted through the system under test.

    Args:
      experimental_requests: The request protocol buffers transmitted through
        the system under test.

    Returns:
      True if the requests satisfy this test scenario; False otherwise.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def verify_responses(self, experimental_responses):
    """Verify the responses transmitted through the system under test.

    Args:
      experimental_responses: The response protocol buffers transmitted through
        the system under test.

    Returns:
      True if the responses satisfy this test scenario; False otherwise.
    """
    raise NotImplementedError()


class EmptyScenario(ProtoScenario):
  """A scenario that transmits no protocol buffers in either direction."""

  def group_and_method(self):
    return 'math.Math', 'DivMany'

  def serialize_request(self, request):
    raise ValueError('This should not be necessary to call!')

  def deserialize_request(self, request_bytestring):
    raise ValueError('This should not be necessary to call!')

  def serialize_response(self, response):
    raise ValueError('This should not be necessary to call!')

  def deserialize_response(self, response_bytestring):
    raise ValueError('This should not be necessary to call!')

  def requests(self):
    return ()

  def response_for_request(self, request):
    raise ValueError('This should not be necessary to call!')

  def verify_requests(self, experimental_requests):
    return not experimental_requests

  def verify_responses(self, experimental_responses):
    return not experimental_responses


class BidirectionallyUnaryScenario(ProtoScenario):
  """A scenario that transmits no protocol buffers in either direction."""

  _DIVIDEND = 59
  _DIVISOR = 7
  _QUOTIENT = 8
  _REMAINDER = 3

  _REQUEST = math_pb2.DivArgs(dividend=_DIVIDEND, divisor=_DIVISOR)
  _RESPONSE = math_pb2.DivReply(quotient=_QUOTIENT, remainder=_REMAINDER)

  def group_and_method(self):
    return 'math.Math', 'Div'

  def serialize_request(self, request):
    return request.SerializeToString()

  def deserialize_request(self, request_bytestring):
    return math_pb2.DivArgs.FromString(request_bytestring)

  def serialize_response(self, response):
    return response.SerializeToString()

  def deserialize_response(self, response_bytestring):
    return math_pb2.DivReply.FromString(response_bytestring)

  def requests(self):
    return [self._REQUEST]

  def response_for_request(self, request):
    return self._RESPONSE

  def verify_requests(self, experimental_requests):
    return tuple(experimental_requests) == (self._REQUEST,)

  def verify_responses(self, experimental_responses):
    return tuple(experimental_responses) == (self._RESPONSE,)


class BidirectionallyStreamingScenario(ProtoScenario):
  """A scenario that transmits no protocol buffers in either direction."""

  _REQUESTS = tuple(
      math_pb2.DivArgs(dividend=59 + index, divisor=7 + index)
      for index in range(test_constants.STREAM_LENGTH))

  def __init__(self):
    self._lock = threading.Lock()
    self._responses = []

  def group_and_method(self):
    return 'math.Math', 'DivMany'

  def serialize_request(self, request):
    return request.SerializeToString()

  def deserialize_request(self, request_bytestring):
    return math_pb2.DivArgs.FromString(request_bytestring)

  def serialize_response(self, response):
    return response.SerializeToString()

  def deserialize_response(self, response_bytestring):
    return math_pb2.DivReply.FromString(response_bytestring)

  def requests(self):
    return self._REQUESTS

  def response_for_request(self, request):
    quotient, remainder = divmod(request.dividend, request.divisor)
    response = math_pb2.DivReply(quotient=quotient, remainder=remainder)
    with self._lock:
      self._responses.append(response)
    return response

  def verify_requests(self, experimental_requests):
    return tuple(experimental_requests) == self._REQUESTS

  def verify_responses(self, experimental_responses):
    with self._lock:
      return tuple(experimental_responses) == tuple(self._responses)
