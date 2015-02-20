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

"""Interfaces of GRPC."""

import abc
import enum


@enum.unique
class Cardinality(enum.Enum):
  """Constants for the four cardinalities of RPC."""

  UNARY_UNARY = 'request-unary/response-unary'
  UNARY_STREAM = 'request-unary/response-streaming'
  STREAM_UNARY = 'request-streaming/response-unary'
  STREAM_STREAM = 'request-streaming/response-streaming'


class RpcMethod(object):
  """A type for the common aspects of RPC method specifications."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def cardinality(self):
    """Identifies the cardinality of this RpcMethod.

    Returns:
      A Cardinality value identifying whether or not this
        RpcMethod is request-unary or request-streaming and
        whether or not it is response-unary or
        response-streaming.
    """
    raise NotImplementedError()


class ClientRpcMethod(RpcMethod):
  """Invocation-side description of an RPC method."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def serialize_request(self, request):
    """Serializes a request value.

    Args:
      request: A request value appropriate for this RpcMethod.

    Returns:
      The serialization of the given request value as a
        bytestring.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def deserialize_response(self, serialized_response):
    """Deserializes a response value.

    Args:
      serialized_response: A bytestring that is the
        serialization of a response value appropriate for this
        RpcMethod.

    Returns:
      A response value corresponding to the given bytestring.
    """
    raise NotImplementedError()


class ServerRpcMethod(RpcMethod):
  """Service-side description of an RPC method."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def deserialize_request(self, serialized_request):
    """Deserializes a request value.

    Args:
      serialized_request: A bytestring that is the
        serialization of a request value appropriate for this
        RpcMethod.

    Returns:
      A request value corresponding to the given bytestring.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def serialize_response(self, response):
    """Serializes a response value.

    Args:
      response: A response value appropriate for this RpcMethod.

    Returns:
      The serialization of the given response value as a
        bytestring.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_unary_unary(self, request):
    """Carries out this RPC.

    This method may only be called if the cardinality of this
    RpcMethod is Cardinality.UNARY_UNARY.

    Args:
      request: A request value appropriate for this RpcMethod.

    Returns:
      A response value appropriate for this RpcMethod.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_unary_stream(self, request):
    """Carries out this RPC.

    This method may only be called if the cardinality of this
    RpcMethod is Cardinality.UNARY_STREAM.

    Args:
      request: A request value appropriate for this RpcMethod.

    Yields:
      Zero or more response values appropriate for this
        RpcMethod.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_stream_unary(self, request_iterator):
    """Carries out this RPC.

    This method may only be called if the cardinality of this
    RpcMethod is Cardinality.STREAM_UNARY.

    Args:
      request_iterator: An iterator of request values
        appropriate for this RpcMethod.

    Returns:
      A response value appropriate for this RpcMethod.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_stream_stream(self, request_iterator):
    """Carries out this RPC.

    This method may only be called if the cardinality of this
    RpcMethod is Cardinality.STREAM_STREAM.

    Args:
      request_iterator: An iterator of request values
        appropriate for this RpcMethod.

    Yields:
      Zero or more response values appropraite for this
        RpcMethod.
    """
    raise NotImplementedError()


class Server(object):
  """A GRPC Server."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def start(self):
    """Instructs this server to commence service of RPCs."""
    raise NotImplementedError()

  @abc.abstractmethod
  def stop(self):
    """Instructs this server to halt service of RPCs."""
    raise NotImplementedError()
