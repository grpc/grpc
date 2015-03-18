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

"""State and behavior for translating between sync and async control flow."""

import threading

from grpc.framework.base import interfaces as base_interfaces
from grpc.framework.face import exceptions
from grpc.framework.face import interfaces
from grpc.framework.foundation import abandonment
from grpc.framework.foundation import stream

INTERNAL_ERROR_LOG_MESSAGE = ':-( RPC Framework (Face) Internal Error! :-('

_OPERATION_OUTCOME_TO_RPC_ABORTION = {
    base_interfaces.Outcome.CANCELLED: interfaces.Abortion.CANCELLED,
    base_interfaces.Outcome.EXPIRED: interfaces.Abortion.EXPIRED,
    base_interfaces.Outcome.RECEPTION_FAILURE:
        interfaces.Abortion.NETWORK_FAILURE,
    base_interfaces.Outcome.TRANSMISSION_FAILURE:
        interfaces.Abortion.NETWORK_FAILURE,
    base_interfaces.Outcome.SERVICED_FAILURE:
        interfaces.Abortion.SERVICED_FAILURE,
    base_interfaces.Outcome.SERVICER_FAILURE:
        interfaces.Abortion.SERVICER_FAILURE,
}


def _as_operation_termination_callback(rpc_abortion_callback):
  def operation_termination_callback(operation_outcome):
    rpc_abortion = _OPERATION_OUTCOME_TO_RPC_ABORTION.get(
        operation_outcome, None)
    if rpc_abortion is not None:
      rpc_abortion_callback(rpc_abortion)
  return operation_termination_callback


def _abortion_outcome_to_exception(abortion_outcome):
  if abortion_outcome == base_interfaces.Outcome.CANCELLED:
    return exceptions.CancellationError()
  elif abortion_outcome == base_interfaces.Outcome.EXPIRED:
    return exceptions.ExpirationError()
  elif abortion_outcome == base_interfaces.Outcome.SERVICER_FAILURE:
    return exceptions.ServicerError()
  elif abortion_outcome == base_interfaces.Outcome.SERVICED_FAILURE:
    return exceptions.ServicedError()
  else:
    return exceptions.NetworkError()


class UnaryConsumer(stream.Consumer):
  """A stream.Consumer that should only ever be passed one value."""

  def __init__(self, on_termination):
    self._on_termination = on_termination
    self._value = None

  def consume(self, value):
    self._value = value

  def terminate(self):
    self._on_termination(self._value)

  def consume_and_terminate(self, value):
    self._on_termination(value)


class Rendezvous(stream.Consumer):
  """A rendez-vous with stream.Consumer and iterator interfaces."""

  def __init__(self):
    self._condition = threading.Condition()
    self._values = []
    self._values_completed = False
    self._abortion = None

  def consume(self, value):
    with self._condition:
      self._values.append(value)
      self._condition.notify()

  def terminate(self):
    with self._condition:
      self._values_completed = True
      self._condition.notify()

  def consume_and_terminate(self, value):
    with self._condition:
      self._values.append(value)
      self._values_completed = True
      self._condition.notify()

  def __iter__(self):
    return self

  def next(self):
    with self._condition:
      while ((self._abortion is None) and
             (not self._values) and
             (not self._values_completed)):
        self._condition.wait()
      if self._abortion is not None:
        raise _abortion_outcome_to_exception(self._abortion)
      elif self._values:
        return self._values.pop(0)
      elif self._values_completed:
        raise StopIteration()
      else:
        raise AssertionError('Unreachable code reached!')

  def set_outcome(self, outcome):
    with self._condition:
      if outcome is not base_interfaces.Outcome.COMPLETED:
        self._abortion = outcome
        self._condition.notify()


class RpcContext(interfaces.RpcContext):
  """A wrapped base_interfaces.OperationContext."""

  def __init__(self, operation_context):
    self._operation_context = operation_context

  def is_active(self):
    return self._operation_context.is_active()

  def time_remaining(self):
    return self._operation_context.time_remaining()

  def add_abortion_callback(self, abortion_callback):
    self._operation_context.add_termination_callback(
        _as_operation_termination_callback(abortion_callback))


def pipe_iterator_to_consumer(iterator, consumer, active, terminate):
  """Pipes values emitted from an iterator to a stream.Consumer.

  Args:
    iterator: An iterator from which values will be emitted.
    consumer: A stream.Consumer to which values will be passed.
    active: A no-argument callable that returns True if the work being done by
      this function is still valid and should not be abandoned and False if the
      work being done by this function should be abandoned.
    terminate: A boolean indicating whether or not this function should
      terminate the given consumer after passing to it all values emitted by the
      given iterator.

  Raises:
    abandonment.Abandoned: If this function quits early after seeing False
      returned by the active function passed to it.
    Exception: This function raises whatever exceptions are raised by iterating
      over the given iterator.
  """
  for element in iterator:
    if not active():
      raise abandonment.Abandoned()

    consumer.consume(element)

    if not active():
      raise abandonment.Abandoned()
  if terminate:
    consumer.terminate()


def abortion_outcome_to_exception(abortion_outcome):
  return _abortion_outcome_to_exception(abortion_outcome)


def as_operation_termination_callback(rpc_abortion_callback):
  return _as_operation_termination_callback(rpc_abortion_callback)
