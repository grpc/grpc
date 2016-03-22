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

import sys
import threading

from grpc.framework.base import interfaces as base_interfaces
from grpc.framework.base import util as base_util
from grpc.framework.face import _control
from grpc.framework.face import interfaces
from grpc.framework.foundation import callable_util
from grpc.framework.foundation import future

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


# NOTE(nathaniel): This class has some extremely special semantics around
# cancellation that allow it to be used by both "blocking" APIs and "futures"
# APIs.
#
# Since futures.Future defines its own exception for cancellation, we want these
# objects, when returned by methods of a returning-Futures-from-other-methods
# object, to raise the same exception for cancellation. But that's weird in a
# blocking API - why should this object, also returned by methods of blocking
# APIs, raise exceptions from the "future" module? Should we do something like
# have this class be parameterized by the type of exception that it raises in
# cancellation circumstances?
#
# We don't have to take such a dramatic step: since blocking APIs define no
# cancellation semantics whatsoever, there is no supported way for
# blocking-API-users of these objects to cancel RPCs, and thus no supported way
# for them to see an exception the type of which would be weird to them.
#
# Bonus: in both blocking and futures APIs, this object still properly raises
# exceptions.CancellationError for any *server-side cancellation* of an RPC.
class _OperationCancellableIterator(interfaces.CancellableIterator):
  """An interfaces.CancellableIterator for response-streaming operations."""

  def __init__(self, rendezvous, operation):
    self._lock = threading.Lock()
    self._rendezvous = rendezvous
    self._operation = operation
    self._cancelled = False

  def __iter__(self):
    return self

  def next(self):
    with self._lock:
      if self._cancelled:
        raise future.CancelledError()
    return next(self._rendezvous)

  def cancel(self):
    with self._lock:
      self._cancelled = True
    self._operation.cancel()
    self._rendezvous.set_outcome(base_interfaces.Outcome.CANCELLED)


class _OperationFuture(future.Future):
  """A future.Future interface to an operation."""

  def __init__(self, rendezvous, operation):
    self._condition = threading.Condition()
    self._rendezvous = rendezvous
    self._operation = operation

    self._cancelled = False
    self._computed = False
    self._payload = None
    self._exception = None
    self._traceback = None
    self._callbacks = []

  def cancel(self):
    """See future.Future.cancel for specification."""
    with self._condition:
      if not self._cancelled and not self._computed:
        self._operation.cancel()
        self._cancelled = True
        self._condition.notify_all()
    return False

  def cancelled(self):
    """See future.Future.cancelled for specification."""
    with self._condition:
      return self._cancelled

  def running(self):
    """See future.Future.running for specification."""
    with self._condition:
      return not self._cancelled and not self._computed

  def done(self):
    """See future.Future.done for specification."""
    with self._condition:
      return self._cancelled or self._computed

  def result(self, timeout=None):
    """See future.Future.result for specification."""
    with self._condition:
      if self._cancelled:
        raise future.CancelledError()
      if self._computed:
        if self._payload is None:
          raise self._exception  # pylint: disable=raising-bad-type
        else:
          return self._payload

      condition = threading.Condition()
      def notify_condition(unused_future):
        with condition:
          condition.notify()
      self._callbacks.append(notify_condition)

    with condition:
      condition.wait(timeout=timeout)

    with self._condition:
      if self._cancelled:
        raise future.CancelledError()
      elif self._computed:
        if self._payload is None:
          raise self._exception  # pylint: disable=raising-bad-type
        else:
          return self._payload
      else:
        raise future.TimeoutError()

  def exception(self, timeout=None):
    """See future.Future.exception for specification."""
    with self._condition:
      if self._cancelled:
        raise future.CancelledError()
      if self._computed:
        return self._exception

      condition = threading.Condition()
      def notify_condition(unused_future):
        with condition:
          condition.notify()
      self._callbacks.append(notify_condition)

    with condition:
      condition.wait(timeout=timeout)

    with self._condition:
      if self._cancelled:
        raise future.CancelledError()
      elif self._computed:
        return self._exception
      else:
        raise future.TimeoutError()

  def traceback(self, timeout=None):
    """See future.Future.traceback for specification."""
    with self._condition:
      if self._cancelled:
        raise future.CancelledError()
      if self._computed:
        return self._traceback

      condition = threading.Condition()
      def notify_condition(unused_future):
        with condition:
          condition.notify()
      self._callbacks.append(notify_condition)

    with condition:
      condition.wait(timeout=timeout)

    with self._condition:
      if self._cancelled:
        raise future.CancelledError()
      elif self._computed:
        return self._traceback
      else:
        raise future.TimeoutError()

  def add_done_callback(self, fn):
    """See future.Future.add_done_callback for specification."""
    with self._condition:
      if self._callbacks is not None:
        self._callbacks.append(fn)
        return

    callable_util.call_logging_exceptions(fn, _DONE_CALLBACK_LOG_MESSAGE, self)

  def on_operation_termination(self, operation_outcome):
    """Indicates to this object that the operation has terminated.

    Args:
      operation_outcome: A base_interfaces.Outcome value indicating the
        outcome of the operation.
    """
    with self._condition:
      cancelled = self._cancelled
      if cancelled:
        callbacks = list(self._callbacks)
        self._callbacks = None
      else:
        rendezvous = self._rendezvous

    if not cancelled:
      payload = None
      exception = None
      traceback = None
      if operation_outcome == base_interfaces.Outcome.COMPLETED:
        try:
          payload = next(rendezvous)
        except Exception as e:  # pylint: disable=broad-except
          exception = e
          traceback = sys.exc_info()[2]
      else:
        try:
          # We raise and then immediately catch in order to create a traceback.
          raise _control.abortion_outcome_to_exception(operation_outcome)
        except Exception as e:  # pylint: disable=broad-except
          exception = e
          traceback = sys.exc_info()[2]
      with self._condition:
        if not self._cancelled:
          self._computed = True
          self._payload = payload
          self._exception = exception
          self._traceback = traceback
        callbacks = list(self._callbacks)
        self._callbacks = None

    for callback in callbacks:
      callable_util.call_logging_exceptions(
          callback, _DONE_CALLBACK_LOG_MESSAGE, self)


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
