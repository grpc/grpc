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

import threading

from _framework.base import interfaces as base_interfaces
from _framework.base import util as base_util
from _framework.face import _control
from _framework.face import interfaces
from _framework.foundation import callable_util
from _framework.foundation import future

_ITERATOR_EXCEPTION_LOG_MESSAGE = 'Exception iterating over requests!'
_DONE_CALLBACK_LOG_MESSAGE = 'Exception calling Future "done" callback!'


class _RendezvousServicedIngestor(base_interfaces.ServicedIngestor):

  def __init__(self, rendezvous):
    self._rendezvous = rendezvous

  def consumer(self, operation_context):
    return self._rendezvous


class _EventServicedIngestor(base_interfaces.ServicedIngestor):

  def __init__(self, result_consumer, abortion_callback):
    self._result_consumer = result_consumer
    self._abortion_callback = abortion_callback

  def consumer(self, operation_context):
    operation_context.add_termination_callback(
        _control.as_operation_termination_callback(self._abortion_callback))
    return self._result_consumer


def _rendezvous_subscription(rendezvous):
  return base_util.full_serviced_subscription(
      _RendezvousServicedIngestor(rendezvous))


def _unary_event_subscription(completion_callback, abortion_callback):
  return base_util.full_serviced_subscription(
      _EventServicedIngestor(
          _control.UnaryConsumer(completion_callback), abortion_callback))


def _stream_event_subscription(result_consumer, abortion_callback):
  return base_util.full_serviced_subscription(
      _EventServicedIngestor(result_consumer, abortion_callback))


class _OperationCancellableIterator(interfaces.CancellableIterator):
  """An interfaces.CancellableIterator for response-streaming operations."""

  def __init__(self, rendezvous, operation):
    self._rendezvous = rendezvous
    self._operation = operation

  def __iter__(self):
    return self

  def next(self):
    return next(self._rendezvous)

  def cancel(self):
    self._operation.cancel()
    self._rendezvous.set_outcome(base_interfaces.Outcome.CANCELLED)


class _OperationFuture(future.Future):
  """A future.Future interface to an operation."""

  def __init__(self, rendezvous, operation):
    self._condition = threading.Condition()
    self._rendezvous = rendezvous
    self._operation = operation

    self._outcome = None
    self._callbacks = []

  def cancel(self):
    """See future.Future.cancel for specification."""
    with self._condition:
      if self._outcome is None:
        self._operation.cancel()
        self._outcome = future.aborted()
        self._condition.notify_all()
    return False

  def cancelled(self):
    """See future.Future.cancelled for specification."""
    return False

  def done(self):
    """See future.Future.done for specification."""
    with self._condition:
      return (self._outcome is not None and
              self._outcome.category is not future.ABORTED)

  def outcome(self):
    """See future.Future.outcome for specification."""
    with self._condition:
      while self._outcome is None:
        self._condition.wait()
      return self._outcome

  def add_done_callback(self, callback):
    """See future.Future.add_done_callback for specification."""
    with self._condition:
      if self._callbacks is not None:
        self._callbacks.add(callback)
        return

      outcome = self._outcome

    callable_util.call_logging_exceptions(
        callback, _DONE_CALLBACK_LOG_MESSAGE, outcome)

  def on_operation_termination(self, operation_outcome):
    """Indicates to this object that the operation has terminated.

    Args:
      operation_outcome: A base_interfaces.Outcome value indicating the
        outcome of the operation.
    """
    with self._condition:
      if (self._outcome is None and
          operation_outcome is not base_interfaces.Outcome.COMPLETED):
        self._outcome = future.raised(
            _control.abortion_outcome_to_exception(operation_outcome))
        self._condition.notify_all()

      outcome = self._outcome
      rendezvous = self._rendezvous
      callbacks = list(self._callbacks)
      self._callbacks = None

    if outcome is None:
      try:
        return_value = next(rendezvous)
      except Exception as e:  # pylint: disable=broad-except
        outcome = future.raised(e)
      else:
        outcome = future.returned(return_value)
      with self._condition:
        if self._outcome is None:
          self._outcome = outcome
          self._condition.notify_all()
        else:
          outcome = self._outcome

    for callback in callbacks:
      callable_util.call_logging_exceptions(
          callback, _DONE_CALLBACK_LOG_MESSAGE, outcome)


class _Call(interfaces.Call):

  def __init__(self, operation):
    self._operation = operation
    self.context = _control.RpcContext(operation.context)

  def cancel(self):
    self._operation.cancel()


def blocking_value_in_value_out(front, name, payload, timeout, trace_id):
  """Services in a blocking fashion a value-in value-out servicer method."""
  rendezvous = _control.Rendezvous()
  subscription = _rendezvous_subscription(rendezvous)
  operation = front.operate(
      name, payload, True, timeout, subscription, trace_id)
  operation.context.add_termination_callback(rendezvous.set_outcome)
  return next(rendezvous)


def future_value_in_value_out(front, name, payload, timeout, trace_id):
  """Services a value-in value-out servicer method by returning a Future."""
  rendezvous = _control.Rendezvous()
  subscription = _rendezvous_subscription(rendezvous)
  operation = front.operate(
      name, payload, True, timeout, subscription, trace_id)
  operation.context.add_termination_callback(rendezvous.set_outcome)
  operation_future = _OperationFuture(rendezvous, operation)
  operation.context.add_termination_callback(
      operation_future.on_operation_termination)
  return operation_future


def inline_value_in_stream_out(front, name, payload, timeout, trace_id):
  """Services a value-in stream-out servicer method."""
  rendezvous = _control.Rendezvous()
  subscription = _rendezvous_subscription(rendezvous)
  operation = front.operate(
      name, payload, True, timeout, subscription, trace_id)
  operation.context.add_termination_callback(rendezvous.set_outcome)
  return _OperationCancellableIterator(rendezvous, operation)


def blocking_stream_in_value_out(
    front, name, payload_iterator, timeout, trace_id):
  """Services in a blocking fashion a stream-in value-out servicer method."""
  rendezvous = _control.Rendezvous()
  subscription = _rendezvous_subscription(rendezvous)
  operation = front.operate(name, None, False, timeout, subscription, trace_id)
  operation.context.add_termination_callback(rendezvous.set_outcome)
  for payload in payload_iterator:
    operation.consumer.consume(payload)
  operation.consumer.terminate()
  return next(rendezvous)


def future_stream_in_value_out(
    front, name, payload_iterator, timeout, trace_id, pool):
  """Services a stream-in value-out servicer method by returning a Future."""
  rendezvous = _control.Rendezvous()
  subscription = _rendezvous_subscription(rendezvous)
  operation = front.operate(name, None, False, timeout, subscription, trace_id)
  operation.context.add_termination_callback(rendezvous.set_outcome)
  pool.submit(
      callable_util.with_exceptions_logged(
          _control.pipe_iterator_to_consumer, _ITERATOR_EXCEPTION_LOG_MESSAGE),
      payload_iterator, operation.consumer, lambda: True, True)
  operation_future = _OperationFuture(rendezvous, operation)
  operation.context.add_termination_callback(
      operation_future.on_operation_termination)
  return operation_future


def inline_stream_in_stream_out(
    front, name, payload_iterator, timeout, trace_id, pool):
  """Services a stream-in stream-out servicer method."""
  rendezvous = _control.Rendezvous()
  subscription = _rendezvous_subscription(rendezvous)
  operation = front.operate(name, None, False, timeout, subscription, trace_id)
  operation.context.add_termination_callback(rendezvous.set_outcome)
  pool.submit(
      callable_util.with_exceptions_logged(
          _control.pipe_iterator_to_consumer, _ITERATOR_EXCEPTION_LOG_MESSAGE),
      payload_iterator, operation.consumer, lambda: True, True)
  return _OperationCancellableIterator(rendezvous, operation)


def event_value_in_value_out(
    front, name, payload, completion_callback, abortion_callback, timeout,
    trace_id):
  subscription = _unary_event_subscription(
      completion_callback, abortion_callback)
  operation = front.operate(
      name, payload, True, timeout, subscription, trace_id)
  return _Call(operation)


def event_value_in_stream_out(
    front, name, payload, result_payload_consumer, abortion_callback, timeout,
    trace_id):
  subscription = _stream_event_subscription(
      result_payload_consumer, abortion_callback)
  operation = front.operate(
      name, payload, True, timeout, subscription, trace_id)
  return _Call(operation)


def event_stream_in_value_out(
    front, name, completion_callback, abortion_callback, timeout, trace_id):
  subscription = _unary_event_subscription(
      completion_callback, abortion_callback)
  operation = front.operate(name, None, False, timeout, subscription, trace_id)
  return _Call(operation), operation.consumer


def event_stream_in_stream_out(
    front, name, result_payload_consumer, abortion_callback, timeout, trace_id):
  subscription = _stream_event_subscription(
      result_payload_consumer, abortion_callback)
  operation = front.operate(name, None, False, timeout, subscription, trace_id)
  return _Call(operation), operation.consumer
