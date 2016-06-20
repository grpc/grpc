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

"""Internal utilities for gRPC Python."""

import collections
import threading
import time

import six

import grpc
from grpc import _common
from grpc.framework.foundation import callable_util

_DONE_CALLBACK_EXCEPTION_LOG_MESSAGE = (
    'Exception calling connectivity future "done" callback!')


class RpcMethodHandler(
    collections.namedtuple(
        '_RpcMethodHandler',
        ('request_streaming', 'response_streaming', 'request_deserializer',
         'response_serializer', 'unary_unary', 'unary_stream', 'stream_unary',
         'stream_stream',)),
    grpc.RpcMethodHandler):
  pass


class DictionaryGenericHandler(grpc.GenericRpcHandler):

  def __init__(self, service, method_handlers):
    self._method_handlers = {
        _common.fully_qualified_method(service, method): method_handler
        for method, method_handler in six.iteritems(method_handlers)}

  def service(self, handler_call_details):
    return self._method_handlers.get(handler_call_details.method)
