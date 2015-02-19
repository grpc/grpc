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

import abc
import collections

from grpc.framework.face import interfaces as face_interfaces

from grpc.early_adopter import interfaces


class _InlineUnaryUnaryMethod(face_interfaces.InlineValueInValueOutMethod):

  def __init__(self, unary_unary_server_rpc_method):
    self._method = unary_unary_server_rpc_method

  def service(self, request, context):
    """See face_interfaces.InlineValueInValueOutMethod.service for spec."""
    return self._method.service_unary_unary(request)


class _InlineUnaryStreamMethod(face_interfaces.InlineValueInStreamOutMethod):

  def __init__(self, unary_stream_server_rpc_method):
    self._method = unary_stream_server_rpc_method

  def service(self, request, context):
    """See face_interfaces.InlineValueInStreamOutMethod.service for spec."""
    return self._method.service_unary_stream(request)


class _InlineStreamUnaryMethod(face_interfaces.InlineStreamInValueOutMethod):

  def __init__(self, stream_unary_server_rpc_method):
    self._method = stream_unary_server_rpc_method

  def service(self, request_iterator, context):
    """See face_interfaces.InlineStreamInValueOutMethod.service for spec."""
    return self._method.service_stream_unary(request_iterator)


class _InlineStreamStreamMethod(face_interfaces.InlineStreamInStreamOutMethod):

  def __init__(self, stream_stream_server_rpc_method):
    self._method = stream_stream_server_rpc_method

  def service(self, request_iterator, context):
    """See face_interfaces.InlineStreamInStreamOutMethod.service for spec."""
    return self._method.service_stream_stream(request_iterator)


class ClientBreakdown(object):
  """An intermediate representation of invocation-side views of RPC methods.

  Attributes:
    request_serializers: A dictionary from RPC method name to callable
      behavior to be used serializing request values for the RPC.
    response_deserializers: A dictionary from RPC method name to callable
      behavior to be used deserializing response values for the RPC.
  """
  __metaclass__ = abc.ABCMeta


class _EasyClientBreakdown(
    ClientBreakdown,
    collections.namedtuple(
        '_EasyClientBreakdown',
        ('request_serializers', 'response_deserializers'))):
  pass


class ServerBreakdown(object):
  """An intermediate representation of implementations of RPC methods.

  Attributes:
    unary_unary_methods: A dictionary from RPC method name to callable
      behavior implementing the RPC method for unary-unary RPC methods.
    unary_stream_methods: A dictionary from RPC method name to callable
      behavior implementing the RPC method for unary-stream RPC methods.
    stream_unary_methods: A dictionary from RPC method name to callable
      behavior implementing the RPC method for stream-unary RPC methods.
    stream_stream_methods: A dictionary from RPC method name to callable
      behavior implementing the RPC method for stream-stream RPC methods.
    request_deserializers: A dictionary from RPC method name to callable
      behavior to be used deserializing request values for the RPC.
    response_serializers: A dictionary from RPC method name to callable
      behavior to be used serializing response values for the RPC.
  """
  __metaclass__ = abc.ABCMeta



class _EasyServerBreakdown(
    ServerBreakdown,
    collections.namedtuple(
        '_EasyServerBreakdown',
        ('unary_unary_methods', 'unary_stream_methods', 'stream_unary_methods',
         'stream_stream_methods', 'request_deserializers',
         'response_serializers'))):
  pass


def client_break_down(methods):
  """Derives a ClientBreakdown from several interfaces.ClientRpcMethods.

  Args:
    methods: A dictionary from RPC mthod name to
      interfaces.ClientRpcMethod object describing the RPCs.

  Returns:
    A ClientBreakdown corresponding to the given methods.
  """
  request_serializers = {}
  response_deserializers = {}
  for name, method in methods.iteritems():
    request_serializers[name] = method.serialize_request
    response_deserializers[name] = method.deserialize_response
  return _EasyClientBreakdown(request_serializers, response_deserializers)


def server_break_down(methods):
  """Derives a ServerBreakdown from several interfaces.ServerRpcMethods.

  Args:
    methods: A dictionary from RPC mthod name to
      interfaces.ServerRpcMethod object describing the RPCs.

  Returns:
    A ServerBreakdown corresponding to the given methods.
  """
  unary_unary = {}
  unary_stream = {}
  stream_unary = {}
  stream_stream = {}
  request_deserializers = {}
  response_serializers = {}
  for name, method in methods.iteritems():
    cardinality = method.cardinality()
    if cardinality is interfaces.Cardinality.UNARY_UNARY:
      unary_unary[name] = _InlineUnaryUnaryMethod(method)
    elif cardinality is interfaces.Cardinality.UNARY_STREAM:
      unary_stream[name] = _InlineUnaryStreamMethod(method)
    elif cardinality is interfaces.Cardinality.STREAM_UNARY:
      stream_unary[name] = _InlineStreamUnaryMethod(method)
    elif cardinality is interfaces.Cardinality.STREAM_STREAM:
      stream_stream[name] = _InlineStreamStreamMethod(method)
    request_deserializers[name] = method.deserialize_request
    response_serializers[name] = method.serialize_response

  return _EasyServerBreakdown(
      unary_unary, unary_stream, stream_unary, stream_stream,
      request_deserializers, response_serializers)
