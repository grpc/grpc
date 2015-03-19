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

"""Utilities for use with GRPC."""

from grpc.framework.alpha import interfaces


class _RpcMethodDescription(
    interfaces.RpcMethodInvocationDescription,
    interfaces.RpcMethodServiceDescription):

  def __init__(
      self, cardinality, unary_unary, unary_stream, stream_unary,
      stream_stream, request_serializer, request_deserializer,
      response_serializer, response_deserializer):
    self._cardinality = cardinality
    self._unary_unary = unary_unary
    self._unary_stream = unary_stream
    self._stream_unary = stream_unary
    self._stream_stream = stream_stream
    self._request_serializer = request_serializer
    self._request_deserializer = request_deserializer
    self._response_serializer = response_serializer
    self._response_deserializer = response_deserializer

  def cardinality(self):
    """See interfaces.RpcMethodDescription.cardinality for specification."""
    return self._cardinality

  def serialize_request(self, request):
    """See interfaces.RpcMethodInvocationDescription.serialize_request."""
    return self._request_serializer(request)

  def deserialize_request(self, serialized_request):
    """See interfaces.RpcMethodServiceDescription.deserialize_request."""
    return self._request_deserializer(serialized_request)

  def serialize_response(self, response):
    """See interfaces.RpcMethodServiceDescription.serialize_response."""
    return self._response_serializer(response)

  def deserialize_response(self, serialized_response):
    """See interfaces.RpcMethodInvocationDescription.deserialize_response."""
    return self._response_deserializer(serialized_response)

  def service_unary_unary(self, request, context):
    """See interfaces.RpcMethodServiceDescription.service_unary_unary."""
    return self._unary_unary(request, context)

  def service_unary_stream(self, request, context):
    """See interfaces.RpcMethodServiceDescription.service_unary_stream."""
    return self._unary_stream(request, context)

  def service_stream_unary(self, request_iterator, context):
    """See interfaces.RpcMethodServiceDescription.service_stream_unary."""
    return self._stream_unary(request_iterator, context)

  def service_stream_stream(self, request_iterator, context):
    """See interfaces.RpcMethodServiceDescription.service_stream_stream."""
    return self._stream_stream(request_iterator, context)


def unary_unary_invocation_description(
    request_serializer, response_deserializer):
  """Creates an interfaces.RpcMethodInvocationDescription for an RPC method.

  Args:
    request_serializer: A callable that when called on a request
      value returns a bytestring corresponding to that value.
    response_deserializer: A callable that when called on a
      bytestring returns the response value corresponding to
      that bytestring.

  Returns:
    An interfaces.RpcMethodInvocationDescription constructed from the given
      arguments representing a unary-request/unary-response RPC method.
  """
  return _RpcMethodDescription(
      interfaces.Cardinality.UNARY_UNARY, None, None, None, None,
      request_serializer, None, None, response_deserializer)


def unary_stream_invocation_description(
    request_serializer, response_deserializer):
  """Creates an interfaces.RpcMethodInvocationDescription for an RPC method.

  Args:
    request_serializer: A callable that when called on a request
      value returns a bytestring corresponding to that value.
    response_deserializer: A callable that when called on a
      bytestring returns the response value corresponding to
      that bytestring.

  Returns:
    An interfaces.RpcMethodInvocationDescription constructed from the given
      arguments representing a unary-request/streaming-response RPC method.
  """
  return _RpcMethodDescription(
      interfaces.Cardinality.UNARY_STREAM, None, None, None, None,
      request_serializer, None, None, response_deserializer)


def stream_unary_invocation_description(
    request_serializer, response_deserializer):
  """Creates an interfaces.RpcMethodInvocationDescription for an RPC method.

  Args:
    request_serializer: A callable that when called on a request
      value returns a bytestring corresponding to that value.
    response_deserializer: A callable that when called on a
      bytestring returns the response value corresponding to
      that bytestring.

  Returns:
    An interfaces.RpcMethodInvocationDescription constructed from the given
      arguments representing a streaming-request/unary-response RPC method.
  """
  return _RpcMethodDescription(
      interfaces.Cardinality.STREAM_UNARY, None, None, None, None,
      request_serializer, None, None, response_deserializer)


def stream_stream_invocation_description(
    request_serializer, response_deserializer):
  """Creates an interfaces.RpcMethodInvocationDescription for an RPC method.

  Args:
    request_serializer: A callable that when called on a request
      value returns a bytestring corresponding to that value.
    response_deserializer: A callable that when called on a
      bytestring returns the response value corresponding to
      that bytestring.

  Returns:
    An interfaces.RpcMethodInvocationDescription constructed from the given
      arguments representing a  streaming-request/streaming-response RPC
      method.
  """
  return _RpcMethodDescription(
      interfaces.Cardinality.STREAM_STREAM, None, None, None, None,
      request_serializer, None, None, response_deserializer)


def unary_unary_service_description(
    behavior, request_deserializer, response_serializer):
  """Creates an interfaces.RpcMethodServiceDescription for the given behavior.

  Args:
    behavior: A callable that implements a unary-unary RPC
      method that accepts a single request and an interfaces.RpcContext and
      returns a single response.
    request_deserializer: A callable that when called on a
      bytestring returns the request value corresponding to that
      bytestring.
    response_serializer: A callable that when called on a
      response value returns the bytestring corresponding to
      that value.

  Returns:
    An interfaces.RpcMethodServiceDescription constructed from the given
      arguments representing a unary-request/unary-response RPC
      method.
  """
  return _RpcMethodDescription(
      interfaces.Cardinality.UNARY_UNARY, behavior, None, None, None,
      None, request_deserializer, response_serializer, None)


def unary_stream_service_description(
    behavior, request_deserializer, response_serializer):
  """Creates an interfaces.RpcMethodServiceDescription for the given behavior.

  Args:
    behavior: A callable that implements a unary-stream RPC
      method that accepts a single request and an interfaces.RpcContext
      and returns an iterator of zero or more responses.
    request_deserializer: A callable that when called on a
      bytestring returns the request value corresponding to that
      bytestring.
    response_serializer: A callable that when called on a
      response value returns the bytestring corresponding to
      that value.

  Returns:
    An interfaces.RpcMethodServiceDescription constructed from the given
      arguments representing a unary-request/streaming-response
      RPC method.
  """
  return _RpcMethodDescription(
      interfaces.Cardinality.UNARY_STREAM, None, behavior, None, None,
      None, request_deserializer, response_serializer, None)


def stream_unary_service_description(
    behavior, request_deserializer, response_serializer):
  """Creates an interfaces.RpcMethodServiceDescription for the given behavior.

  Args:
    behavior: A callable that implements a stream-unary RPC
      method that accepts an iterator of zero or more requests
      and an interfaces.RpcContext and returns a single response.
    request_deserializer: A callable that when called on a
      bytestring returns the request value corresponding to that
      bytestring.
    response_serializer: A callable that when called on a
      response value returns the bytestring corresponding to
      that value.

  Returns:
    An interfaces.RpcMethodServiceDescription constructed from the given
      arguments representing a streaming-request/unary-response
      RPC method.
  """
  return _RpcMethodDescription(
      interfaces.Cardinality.STREAM_UNARY, None, None, behavior, None,
      None, request_deserializer, response_serializer, None)


def stream_stream_service_description(
    behavior, request_deserializer, response_serializer):
  """Creates an interfaces.RpcMethodServiceDescription for the given behavior.

  Args:
    behavior: A callable that implements a stream-stream RPC
      method that accepts an iterator of zero or more requests
      and an interfaces.RpcContext and returns an iterator of
      zero or more responses.
    request_deserializer: A callable that when called on a
      bytestring returns the request value corresponding to that
      bytestring.
    response_serializer: A callable that when called on a
      response value returns the bytestring corresponding to
      that value.

  Returns:
    An interfaces.RpcMethodServiceDescription constructed from the given
      arguments representing a
      streaming-request/streaming-response RPC method.
  """
  return _RpcMethodDescription(
      interfaces.Cardinality.STREAM_STREAM, None, None, None, behavior,
      None, request_deserializer, response_serializer, None)
