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

"""Behaviors for servicing RPCs."""

from grpc.framework.crust import _control
from grpc.framework.foundation import abandonment
from grpc.framework.interfaces.base import utilities
from grpc.framework.interfaces.face import face


class _ServicerContext(face.ServicerContext):

  def __init__(self, rendezvous):
    self._rendezvous = rendezvous

  def is_active(self):
    return self._rendezvous.is_active()

  def time_remaining(self):
    return self._rendezvous.time_remaining()

  def add_abortion_callback(self, abortion_callback):
    return self._rendezvous.add_abortion_callback(abortion_callback)

  def cancel(self):
    self._rendezvous.cancel()

  def protocol_context(self):
    return self._rendezvous.protocol_context()

  def invocation_metadata(self):
    return self._rendezvous.initial_metadata()

  def initial_metadata(self, initial_metadata):
    self._rendezvous.set_initial_metadata(initial_metadata)

  def terminal_metadata(self, terminal_metadata):
    self._rendezvous.set_terminal_metadata(terminal_metadata)

  def code(self, code):
    self._rendezvous.set_code(code)

  def details(self, details):
    self._rendezvous.set_details(details)


def _adaptation(pool, in_pool):
  def adaptation(operator, operation_context):
    rendezvous = _control.Rendezvous(operator, operation_context)
    subscription = utilities.full_subscription(
        rendezvous, _control.protocol_receiver(rendezvous))
    outcome = operation_context.add_termination_callback(rendezvous.set_outcome)
    if outcome is None:
      pool.submit(_control.pool_wrap(in_pool, operation_context), rendezvous)
      return subscription
    else:
      raise abandonment.Abandoned()
  return adaptation


def adapt_inline_unary_unary(method, pool):
  def in_pool(rendezvous):
    request = next(rendezvous)
    response = method(request, _ServicerContext(rendezvous))
    rendezvous.consume_and_terminate(response)
  return _adaptation(pool, in_pool)


def adapt_inline_unary_stream(method, pool):
  def in_pool(rendezvous):
    request = next(rendezvous)
    response_iterator = method(request, _ServicerContext(rendezvous))
    for response in response_iterator:
      rendezvous.consume(response)
    rendezvous.terminate()
  return _adaptation(pool, in_pool)


def adapt_inline_stream_unary(method, pool):
  def in_pool(rendezvous):
    response = method(rendezvous, _ServicerContext(rendezvous))
    rendezvous.consume_and_terminate(response)
  return _adaptation(pool, in_pool)


def adapt_inline_stream_stream(method, pool):
  def in_pool(rendezvous):
    response_iterator = method(rendezvous, _ServicerContext(rendezvous))
    for response in response_iterator:
      rendezvous.consume(response)
    rendezvous.terminate()
  return _adaptation(pool, in_pool)


def adapt_event_unary_unary(method, pool):
  def in_pool(rendezvous):
    request = next(rendezvous)
    method(
        request, rendezvous.consume_and_terminate, _ServicerContext(rendezvous))
  return _adaptation(pool, in_pool)


def adapt_event_unary_stream(method, pool):
  def in_pool(rendezvous):
    request = next(rendezvous)
    method(request, rendezvous, _ServicerContext(rendezvous))
  return _adaptation(pool, in_pool)


def adapt_event_stream_unary(method, pool):
  def in_pool(rendezvous):
    request_consumer = method(
        rendezvous.consume_and_terminate, _ServicerContext(rendezvous))
    for request in rendezvous:
      request_consumer.consume(request)
    request_consumer.terminate()
  return _adaptation(pool, in_pool)


def adapt_event_stream_stream(method, pool):
  def in_pool(rendezvous):
    request_consumer = method(rendezvous, _ServicerContext(rendezvous))
    for request in rendezvous:
      request_consumer.consume(request)
    request_consumer.terminate()
  return _adaptation(pool, in_pool)


def adapt_multi_method(multi_method, pool):
  def adaptation(group, method, operator, operation_context):
    rendezvous = _control.Rendezvous(operator, operation_context)
    subscription = utilities.full_subscription(
        rendezvous, _control.protocol_receiver(rendezvous))
    outcome = operation_context.add_termination_callback(rendezvous.set_outcome)
    if outcome is None:
      def in_pool():
        request_consumer = multi_method.service(
            group, method, rendezvous, _ServicerContext(rendezvous))
        for request in rendezvous:
          request_consumer.consume(request)
        request_consumer.terminate()
      pool.submit(_control.pool_wrap(in_pool, operation_context), rendezvous)
      return subscription
    else:
      raise abandonment.Abandoned()
  return adaptation
