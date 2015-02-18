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

"""Utility for serialization in the context of test RPC services."""

import collections


class Serialization(
    collections.namedtuple(
        '_Serialization',
        ['request_serializers',
         'request_deserializers',
         'response_serializers',
         'response_deserializers'])):
  """An aggregation of serialization behaviors for an RPC service.

  Attributes:
    request_serializers: A dict from method name to request object serializer
      behavior.
    request_deserializers: A dict from method name to request object
      deserializer behavior.
    response_serializers: A dict from method name to response object serializer
      behavior.
    response_deserializers: A dict from method name to response object
      deserializer behavior.
  """


def serialization(methods):
  """Creates a Serialization from a sequences of interfaces.Method objects."""
  request_serializers = {}
  request_deserializers = {}
  response_serializers = {}
  response_deserializers = {}
  for method in methods:
    name = method.name()
    request_serializers[name] = method.serialize_request
    request_deserializers[name] = method.deserialize_request
    response_serializers[name] = method.serialize_response
    response_deserializers[name] = method.deserialize_response
  return Serialization(
      request_serializers, request_deserializers, response_serializers,
      response_deserializers)
