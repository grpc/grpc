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

"""Utilities for the face layer of RPC Framework."""

# stream is referenced from specification in this module.
from grpc.framework.face import interfaces
from grpc.framework.foundation import stream  # pylint: disable=unused-import


class _InlineUnaryUnaryMethod(interfaces.InlineValueInValueOutMethod):

  def __init__(self, behavior):
    self._behavior = behavior

  def service(self, request, context):
    return self._behavior(request, context)


class _InlineUnaryStreamMethod(interfaces.InlineValueInStreamOutMethod):

  def __init__(self, behavior):
    self._behavior = behavior

  def service(self, request, context):
    return self._behavior(request, context)


class _InlineStreamUnaryMethod(interfaces.InlineStreamInValueOutMethod):

  def __init__(self, behavior):
    self._behavior = behavior

  def service(self, request_iterator, context):
    return self._behavior(request_iterator, context)


class _InlineStreamStreamMethod(interfaces.InlineStreamInStreamOutMethod):

  def __init__(self, behavior):
    self._behavior = behavior

  def service(self, request_iterator, context):
    return self._behavior(request_iterator, context)


class _EventUnaryUnaryMethod(interfaces.EventValueInValueOutMethod):

  def __init__(self, behavior):
    self._behavior = behavior

  def service(self, request, response_callback, context):
    return self._behavior(request, response_callback, context)


class _EventUnaryStreamMethod(interfaces.EventValueInStreamOutMethod):

  def __init__(self, behavior):
    self._behavior = behavior

  def service(self, request, response_consumer, context):
    return self._behavior(request, response_consumer, context)


class _EventStreamUnaryMethod(interfaces.EventStreamInValueOutMethod):

  def __init__(self, behavior):
    self._behavior = behavior

  def service(self, response_callback, context):
    return self._behavior(response_callback, context)


class _EventStreamStreamMethod(interfaces.EventStreamInStreamOutMethod):

  def __init__(self, behavior):
    self._behavior = behavior

  def service(self, response_consumer, context):
    return self._behavior(response_consumer, context)


def inline_unary_unary_method(behavior):
  """Creates an interfaces.InlineValueInValueOutMethod from a behavior.

  Args:
    behavior: The implementation of a unary-unary RPC method as a callable
      value that takes a request value and an interfaces.RpcContext object and
      returns a response value.

  Returns:
    An interfaces.InlineValueInValueOutMethod derived from the given behavior.
  """
  return _InlineUnaryUnaryMethod(behavior)


def inline_unary_stream_method(behavior):
  """Creates an interfaces.InlineValueInStreamOutMethod from a behavior.

  Args:
    behavior: The implementation of a unary-stream RPC method as a callable
      value that takes a request value and an interfaces.RpcContext object and
      returns an iterator of response values.

  Returns:
    An interfaces.InlineValueInStreamOutMethod derived from the given behavior.
  """
  return _InlineUnaryStreamMethod(behavior)


def inline_stream_unary_method(behavior):
  """Creates an interfaces.InlineStreamInValueOutMethod from a behavior.

  Args:
    behavior: The implementation of a stream-unary RPC method as a callable
      value that takes an iterator of request values and an
      interfaces.RpcContext object and returns a response value.

  Returns:
    An interfaces.InlineStreamInValueOutMethod derived from the given behavior.
  """
  return _InlineStreamUnaryMethod(behavior)


def inline_stream_stream_method(behavior):
  """Creates an interfaces.InlineStreamInStreamOutMethod from a behavior.

  Args:
    behavior: The implementation of a stream-stream RPC method as a callable
      value that takes an iterator of request values and an
      interfaces.RpcContext object and returns an iterator of response values.

  Returns:
    An interfaces.InlineStreamInStreamOutMethod derived from the given
      behavior.
  """
  return _InlineStreamStreamMethod(behavior)


def event_unary_unary_method(behavior):
  """Creates an interfaces.EventValueInValueOutMethod from a behavior.

  Args:
    behavior: The implementation of a unary-unary RPC method as a callable
      value that takes a request value, a response callback to which to pass
      the response value of the RPC, and an interfaces.RpcContext.

  Returns:
    An interfaces.EventValueInValueOutMethod derived from the given behavior.
  """
  return _EventUnaryUnaryMethod(behavior)


def event_unary_stream_method(behavior):
  """Creates an interfaces.EventValueInStreamOutMethod from a behavior.

  Args:
    behavior: The implementation of a unary-stream RPC method as a callable
      value that takes a request value, a stream.Consumer to which to pass the
      response values of the RPC, and an interfaces.RpcContext.

  Returns:
    An interfaces.EventValueInStreamOutMethod derived from the given behavior.
  """
  return _EventUnaryStreamMethod(behavior)


def event_stream_unary_method(behavior):
  """Creates an interfaces.EventStreamInValueOutMethod from a behavior.

  Args:
    behavior: The implementation of a stream-unary RPC method as a callable
      value that takes a response callback to which to pass the response value
      of the RPC and an interfaces.RpcContext and returns a stream.Consumer to
      which the request values of the RPC should be passed.

  Returns:
    An interfaces.EventStreamInValueOutMethod derived from the given behavior.
  """
  return _EventStreamUnaryMethod(behavior)


def event_stream_stream_method(behavior):
  """Creates an interfaces.EventStreamInStreamOutMethod from a behavior.

  Args:
    behavior: The implementation of a stream-stream RPC method as a callable
      value that takes a stream.Consumer to which to pass the response values
      of the RPC and an interfaces.RpcContext and returns a stream.Consumer to
      which the request values of the RPC should be passed.

  Returns:
    An interfaces.EventStreamInStreamOutMethod derived from the given behavior.
  """
  return _EventStreamStreamMethod(behavior)
