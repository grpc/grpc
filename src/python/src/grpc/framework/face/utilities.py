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

"""Utilities for RPC framework's face layer."""

import collections

from grpc.framework.common import cardinality
from grpc.framework.common import style
from grpc.framework.face import interfaces
from grpc.framework.foundation import stream


class _MethodImplementation(
    interfaces.MethodImplementation,
    collections.namedtuple(
        '_MethodImplementation',
        ['cardinality', 'style', 'unary_unary_inline', 'unary_stream_inline',
         'stream_unary_inline', 'stream_stream_inline', 'unary_unary_event',
         'unary_stream_event', 'stream_unary_event', 'stream_stream_event',])):
  pass


def unary_unary_inline(behavior):
  """Creates an interfaces.MethodImplementation for the given behavior.

  Args:
    behavior: The implementation of a unary-unary RPC method as a callable value
      that takes a request value and an interfaces.RpcContext object and
      returns a response value.

  Returns:
    An interfaces.MethodImplementation derived from the given behavior.
  """
  return _MethodImplementation(
      cardinality.Cardinality.UNARY_UNARY, style.Service.INLINE, behavior,
      None, None, None, None, None, None, None)


def unary_stream_inline(behavior):
  """Creates an interfaces.MethodImplementation for the given behavior.

  Args:
    behavior: The implementation of a unary-stream RPC method as a callable
      value that takes a request value and an interfaces.RpcContext object and
      returns an iterator of response values.

  Returns:
    An interfaces.MethodImplementation derived from the given behavior.
  """
  return _MethodImplementation(
      cardinality.Cardinality.UNARY_STREAM, style.Service.INLINE, None,
      behavior, None, None, None, None, None, None)


def stream_unary_inline(behavior):
  """Creates an interfaces.MethodImplementation for the given behavior.

  Args:
    behavior: The implementation of a stream-unary RPC method as a callable
      value that takes an iterator of request values and an
      interfaces.RpcContext object and returns a response value.

  Returns:
    An interfaces.MethodImplementation derived from the given behavior.
  """
  return _MethodImplementation(
      cardinality.Cardinality.STREAM_UNARY, style.Service.INLINE, None, None,
      behavior, None, None, None, None, None)


def stream_stream_inline(behavior):
  """Creates an interfaces.MethodImplementation for the given behavior.

  Args:
    behavior: The implementation of a stream-stream RPC method as a callable
      value that takes an iterator of request values and an
      interfaces.RpcContext object and returns an iterator of response values.

  Returns:
    An interfaces.MethodImplementation derived from the given behavior.
  """
  return _MethodImplementation(
      cardinality.Cardinality.STREAM_STREAM, style.Service.INLINE, None, None,
      None, behavior, None, None, None, None)


def unary_unary_event(behavior):
  """Creates an interfaces.MethodImplementation for the given behavior.

  Args:
    behavior: The implementation of a unary-unary RPC method as a callable
      value that takes a request value, a response callback to which to pass
      the response value of the RPC, and an interfaces.RpcContext.

  Returns:
    An interfaces.MethodImplementation derived from the given behavior.
  """
  return _MethodImplementation(
      cardinality.Cardinality.UNARY_UNARY, style.Service.EVENT, None, None,
      None, None, behavior, None, None, None)


def unary_stream_event(behavior):
  """Creates an interfaces.MethodImplementation for the given behavior.

  Args:
    behavior: The implementation of a unary-stream RPC method as a callable
      value that takes a request value, a stream.Consumer to which to pass the
      the response values of the RPC, and an interfaces.RpcContext.

  Returns:
    An interfaces.MethodImplementation derived from the given behavior.
  """
  return _MethodImplementation(
      cardinality.Cardinality.UNARY_STREAM, style.Service.EVENT, None, None,
      None, None, None, behavior, None, None)


def stream_unary_event(behavior):
  """Creates an interfaces.MethodImplementation for the given behavior.

  Args:
    behavior: The implementation of a stream-unary RPC method as a callable
      value that takes a response callback to which to pass the response value
      of the RPC and an interfaces.RpcContext and returns a stream.Consumer to
      which the request values of the RPC should be passed.

  Returns:
    An interfaces.MethodImplementation derived from the given behavior.
  """
  return _MethodImplementation(
      cardinality.Cardinality.STREAM_UNARY, style.Service.EVENT, None, None,
      None, None, None, None, behavior, None)


def stream_stream_event(behavior):
  """Creates an interfaces.MethodImplementation for the given behavior.

  Args:
    behavior: The implementation of a stream-stream RPC method as a callable
      value that takes a stream.Consumer to which to pass the response values
      of the RPC and an interfaces.RpcContext and returns a stream.Consumer to
      which the request values of the RPC should be passed.

  Returns:
    An interfaces.MethodImplementation derived from the given behavior.
  """
  return _MethodImplementation(
      cardinality.Cardinality.STREAM_STREAM, style.Service.EVENT, None, None,
      None, None, None, None, None, behavior)
