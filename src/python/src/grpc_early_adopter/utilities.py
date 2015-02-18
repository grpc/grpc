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

from grpc_early_adopter import interfaces


class _RpcMethod(interfaces.RpcMethod):

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
    """See interfaces.RpcMethod.cardinality for specification."""
    return self._cardinality

  def serialize_request(self, request):
    """See interfaces.RpcMethod.serialize_request for specification."""
    return self._request_serializer(request)

  def deserialize_request(self, serialized_request):
    """See interfaces.RpcMethod.deserialize_request for specification."""
    return self._request_deserializer(serialized_request)

  def serialize_response(self, response):
    """See interfaces.RpcMethod.serialize_response for specification."""
    return self._response_serializer(response)

  def deserialize_response(self, serialized_response):
    """See interfaces.RpcMethod.deserialize_response for specification."""
    return self._response_deserializer(serialized_response)

  def service_unary_unary(self, request):
    """See interfaces.RpcMethod.service_unary_unary for specification."""
    return self._unary_unary(request)

  def service_unary_stream(self, request):
    """See interfaces.RpcMethod.service_unary_stream for specification."""
    return self._unary_stream(request)

  def service_stream_unary(self, request_iterator):
    """See interfaces.RpcMethod.service_stream_unary for specification."""
    return self._stream_unary(request_iterator)

  def service_stream_stream(self, request_iterator):
    """See interfaces.RpcMethod.service_stream_stream for specification."""
    return self._stream_stream(request_iterator)


def unary_unary_rpc_method(
    behavior, request_serializer, request_deserializer, response_serializer,
    response_deserializer):
  """Constructs an interfaces.RpcMethod for the given behavior.

  Args:
    behavior: A callable that implements a unary-unary RPC
      method that accepts a single request and returns a single
      response.
    request_serializer: A callable that when called on a request
      value returns a bytestring corresponding to that value.
    request_deserializer: A callable that when called on a
      bytestring returns the request value corresponding to that
      bytestring.
    response_serializer: A callable that when called on a
      response value returns the bytestring corresponding to
      that value.
    response_deserializer: A callable that when called on a
      bytestring returns the response value corresponding to
      that bytestring.

  Returns:
    An interfaces.RpcMethod constructed from the given
      arguments representing a unary-request/unary-response RPC
      method.
  """
  return _RpcMethod(
      interfaces.Cardinality.UNARY_UNARY, behavior, None, None, None,
      request_serializer, request_deserializer, response_serializer,
      response_deserializer)


def unary_stream_rpc_method(
    behavior, request_serializer, request_deserializer, response_serializer,
    response_deserializer):
  """Constructs an interfaces.RpcMethod for the given behavior.

  Args:
    behavior: A callable that implements a unary-stream RPC
      method that accepts a single request and returns an
      iterator of zero or more responses.
    request_serializer: A callable that when called on a request
      value returns a bytestring corresponding to that value.
    request_deserializer: A callable that when called on a
      bytestring returns the request value corresponding to that
      bytestring.
    response_serializer: A callable that when called on a
      response value returns the bytestring corresponding to
      that value.
    response_deserializer: A callable that when called on a
      bytestring returns the response value corresponding to
      that bytestring.

  Returns:
    An interfaces.RpcMethod constructed from the given
      arguments representing a unary-request/streaming-response
      RPC method.
  """
  return _RpcMethod(
      interfaces.Cardinality.UNARY_STREAM, None, behavior, None, None,
      request_serializer, request_deserializer, response_serializer,
      response_deserializer)


def stream_unary_rpc_method(
    behavior, request_serializer, request_deserializer, response_serializer,
    response_deserializer):
  """Constructs an interfaces.RpcMethod for the given behavior.

  Args:
    behavior: A callable that implements a stream-unary RPC
      method that accepts an iterator of zero or more requests
      and returns a single response.
    request_serializer: A callable that when called on a request
      value returns a bytestring corresponding to that value.
    request_deserializer: A callable that when called on a
      bytestring returns the request value corresponding to that
      bytestring.
    response_serializer: A callable that when called on a
      response value returns the bytestring corresponding to
      that value.
    response_deserializer: A callable that when called on a
      bytestring returns the response value corresponding to
      that bytestring.

  Returns:
    An interfaces.RpcMethod constructed from the given
      arguments representing a streaming-request/unary-response
      RPC method.
  """
  return _RpcMethod(
      interfaces.Cardinality.STREAM_UNARY, None, None, behavior, None,
      request_serializer, request_deserializer, response_serializer,
      response_deserializer)


def stream_stream_rpc_method(
    behavior, request_serializer, request_deserializer, response_serializer,
    response_deserializer):
  """Constructs an interfaces.RpcMethod for the given behavior.

  Args:
    behavior: A callable that implements a stream-stream RPC
      method that accepts an iterator of zero or more requests
      and returns an iterator of zero or more responses.
    request_serializer: A callable that when called on a request
      value returns a bytestring corresponding to that value.
    request_deserializer: A callable that when called on a
      bytestring returns the request value corresponding to that
      bytestring.
    response_serializer: A callable that when called on a
      response value returns the bytestring corresponding to
      that value.
    response_deserializer: A callable that when called on a
      bytestring returns the response value corresponding to
      that bytestring.

  Returns:
    An interfaces.RpcMethod constructed from the given
      arguments representing a
      streaming-request/streaming-response RPC method.
  """
  return _RpcMethod(
      interfaces.Cardinality.STREAM_STREAM, None, None, None, behavior,
      request_serializer, request_deserializer, response_serializer,
      response_deserializer)
