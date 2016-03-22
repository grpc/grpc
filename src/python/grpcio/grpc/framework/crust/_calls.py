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

"""Utility functions for invoking RPCs."""

from grpc.framework.crust import _control
from grpc.framework.interfaces.base import utilities
from grpc.framework.interfaces.face import face

_ITERATOR_EXCEPTION_LOG_MESSAGE = 'Exception iterating over requests!'

_EMPTY_COMPLETION = utilities.completion(None, None, None)


def _invoke(
    end, group, method, timeout, protocol_options, initial_metadata, payload,
    complete):
  rendezvous = _control.Rendezvous(None, None)
  subscription = utilities.full_subscription(
      rendezvous, _control.protocol_receiver(rendezvous))
  operation_context, operator = end.operate(
      group, method, subscription, timeout, protocol_options=protocol_options,
      initial_metadata=initial_metadata, payload=payload,
      completion=_EMPTY_COMPLETION if complete else None)
  rendezvous.set_operator_and_context(operator, operation_context)
  outcome = operation_context.add_termination_callback(rendezvous.set_outcome)
  if outcome is not None:
    rendezvous.set_outcome(outcome)
  return rendezvous, operation_context, outcome


def _event_return_unary(
    receiver, abortion_callback, rendezvous, operation_context, outcome, pool):
  if outcome is None:
    def in_pool():
      abortion = rendezvous.add_abortion_callback(abortion_callback)
      if abortion is None:
        try:
          receiver.initial_metadata(rendezvous.initial_metadata())
          receiver.response(next(rendezvous))
          receiver.complete(
              rendezvous.terminal_metadata(), rendezvous.code(),
              rendezvous.details())
        except face.AbortionError:
          pass
      else:
        abortion_callback(abortion)
    pool.submit(_control.pool_wrap(in_pool, operation_context))
  return rendezvous


def _event_return_stream(
    receiver, abortion_callback, rendezvous, operation_context, outcome, pool):
  if outcome is None:
    def in_pool():
      abortion = rendezvous.add_abortion_callback(abortion_callback)
      if abortion is None:
        try:
          receiver.initial_metadata(rendezvous.initial_metadata())
          for response in rendezvous:
            receiver.response(response)
          receiver.complete(
              rendezvous.terminal_metadata(), rendezvous.code(),
              rendezvous.details())
        except face.AbortionError:
          pass
      else:
        abortion_callback(abortion)
    pool.submit(_control.pool_wrap(in_pool, operation_context))
  return rendezvous


def blocking_unary_unary(
    end, group, method, timeout, with_call, protocol_options, initial_metadata,
    payload):
  """Services in a blocking fashion a unary-unary servicer method."""
  rendezvous, unused_operation_context, unused_outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, payload,
      True)
  if with_call:
    return next(rendezvous), rendezvous
  else:
    return next(rendezvous)


def future_unary_unary(
    end, group, method, timeout, protocol_options, initial_metadata, payload):
  """Services a value-in value-out servicer method by returning a Future."""
  rendezvous, unused_operation_context, unused_outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, payload,
      True)
  return rendezvous


def inline_unary_stream(
    end, group, method, timeout, protocol_options, initial_metadata, payload):
  """Services a value-in stream-out servicer method."""
  rendezvous, unused_operation_context, unused_outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, payload,
      True)
  return rendezvous


def blocking_stream_unary(
    end, group, method, timeout, with_call, protocol_options, initial_metadata,
    payload_iterator, pool):
  """Services in a blocking fashion a stream-in value-out servicer method."""
  rendezvous, operation_context, outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, None,
      False)
  if outcome is None:
    def in_pool():
      for payload in payload_iterator:
        rendezvous.consume(payload)
      rendezvous.terminate()
    pool.submit(_control.pool_wrap(in_pool, operation_context))
    if with_call:
      return next(rendezvous), rendezvous
    else:
      return next(rendezvous)
  else:
    if with_call:
      return next(rendezvous), rendezvous
    else:
      return next(rendezvous)


def future_stream_unary(
    end, group, method, timeout, protocol_options, initial_metadata,
    payload_iterator, pool):
  """Services a stream-in value-out servicer method by returning a Future."""
  rendezvous, operation_context, outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, None,
      False)
  if outcome is None:
    def in_pool():
      for payload in payload_iterator:
        rendezvous.consume(payload)
      rendezvous.terminate()
    pool.submit(_control.pool_wrap(in_pool, operation_context))
  return rendezvous


def inline_stream_stream(
    end, group, method, timeout, protocol_options, initial_metadata,
    payload_iterator, pool):
  """Services a stream-in stream-out servicer method."""
  rendezvous, operation_context, outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, None,
      False)
  if outcome is None:
    def in_pool():
      for payload in payload_iterator:
        rendezvous.consume(payload)
      rendezvous.terminate()
    pool.submit(_control.pool_wrap(in_pool, operation_context))
  return rendezvous


def event_unary_unary(
    end, group, method, timeout, protocol_options, initial_metadata, payload,
    receiver, abortion_callback, pool):
  rendezvous, operation_context, outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, payload,
      True)
  return _event_return_unary(
      receiver, abortion_callback, rendezvous, operation_context, outcome, pool)


def event_unary_stream(
    end, group, method, timeout, protocol_options, initial_metadata, payload,
    receiver, abortion_callback, pool):
  rendezvous, operation_context, outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, payload,
      True)
  return _event_return_stream(
      receiver, abortion_callback, rendezvous, operation_context, outcome, pool)


def event_stream_unary(
    end, group, method, timeout, protocol_options, initial_metadata, receiver,
    abortion_callback, pool):
  rendezvous, operation_context, outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, None,
      False)
  return _event_return_unary(
      receiver, abortion_callback, rendezvous, operation_context, outcome, pool)


def event_stream_stream(
    end, group, method, timeout, protocol_options, initial_metadata, receiver,
    abortion_callback, pool):
  rendezvous, operation_context, outcome = _invoke(
      end, group, method, timeout, protocol_options, initial_metadata, None,
      False)
  return _event_return_stream(
      receiver, abortion_callback, rendezvous, operation_context, outcome, pool)
