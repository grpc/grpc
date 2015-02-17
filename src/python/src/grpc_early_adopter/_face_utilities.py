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

from _framework.face import interfaces as face_interfaces

from grpc_early_adopter import interfaces


class _InlineUnaryUnaryMethod(face_interfaces.InlineValueInValueOutMethod):

  def __init__(self, unary_unary_rpc_method):
    self._method = unary_unary_rpc_method

  def service(self, request, context):
    """See face_interfaces.InlineValueInValueOutMethod.service for spec."""
    return self._method.service_unary_unary(request)


class _InlineUnaryStreamMethod(face_interfaces.InlineValueInStreamOutMethod):

  def __init__(self, unary_stream_rpc_method):
    self._method = unary_stream_rpc_method

  def service(self, request, context):
    """See face_interfaces.InlineValueInStreamOutMethod.service for spec."""
    return self._method.service_unary_stream(request)


class _InlineStreamUnaryMethod(face_interfaces.InlineStreamInValueOutMethod):

  def __init__(self, stream_unary_rpc_method):
    self._method = stream_unary_rpc_method

  def service(self, request_iterator, context):
    """See face_interfaces.InlineStreamInValueOutMethod.service for spec."""
    return self._method.service_stream_unary(request_iterator)


class _InlineStreamStreamMethod(face_interfaces.InlineStreamInStreamOutMethod):

  def __init__(self, stream_stream_rpc_method):
    self._method = stream_stream_rpc_method

  def service(self, request_iterator, context):
    """See face_interfaces.InlineStreamInStreamOutMethod.service for spec."""
    return self._method.service_stream_stream(request_iterator)


class Breakdown(object):
  """An intermediate representation of implementations of RPC methods.

  Attributes:
    unary_unary_methods:
    unary_stream_methods:
    stream_unary_methods:
    stream_stream_methods:
    request_serializers:
    request_deserializers:
    response_serializers:
    response_deserializers:
  """
  __metaclass__ = abc.ABCMeta



class _EasyBreakdown(
    Breakdown,
    collections.namedtuple(
        '_EasyBreakdown',
        ['unary_unary_methods', 'unary_stream_methods', 'stream_unary_methods',
         'stream_stream_methods', 'request_serializers',
         'request_deserializers', 'response_serializers',
         'response_deserializers'])):
  pass


def break_down(methods):
  """Breaks down RPC methods.

  Args:
    methods: A dictionary from RPC mthod name to
      interfaces.RpcMethod object describing the RPCs.

  Returns:
    A Breakdown corresponding to the given methods.
  """
  unary_unary = {}
  unary_stream = {}
  stream_unary = {}
  stream_stream = {}
  request_serializers = {}
  request_deserializers = {}
  response_serializers = {}
  response_deserializers = {}

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
    request_serializers[name] = method.serialize_request
    request_deserializers[name] = method.deserialize_request
    response_serializers[name] = method.serialize_response
    response_deserializers[name] = method.deserialize_response

  return _EasyBreakdown(
      unary_unary, unary_stream, stream_unary, stream_stream,
      request_serializers, request_deserializers, response_serializers,
      response_deserializers)
