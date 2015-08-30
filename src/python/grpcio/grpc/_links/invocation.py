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

"""The RPC-invocation-side bridge between RPC Framework and GRPC-on-the-wire."""

import abc
import enum
import logging
import threading
import time

from grpc._adapter import _intermediary_low
from grpc.framework.foundation import activated
from grpc.framework.foundation import logging_pool
from grpc.framework.foundation import relay
from grpc.framework.interfaces.links import links

_STOP = _intermediary_low.Event.Kind.STOP
_WRITE = _intermediary_low.Event.Kind.WRITE_ACCEPTED
_COMPLETE = _intermediary_low.Event.Kind.COMPLETE_ACCEPTED
_READ = _intermediary_low.Event.Kind.READ_ACCEPTED
_METADATA = _intermediary_low.Event.Kind.METADATA_ACCEPTED
_FINISH = _intermediary_low.Event.Kind.FINISH


@enum.unique
class _Read(enum.Enum):
  AWAITING_METADATA = 'awaiting metadata'
  READING = 'reading'
  AWAITING_ALLOWANCE = 'awaiting allowance'
  CLOSED = 'closed'


@enum.unique
class _HighWrite(enum.Enum):
  OPEN = 'open'
  CLOSED = 'closed'


@enum.unique
class _LowWrite(enum.Enum):
  OPEN = 'OPEN'
  ACTIVE = 'ACTIVE'
  CLOSED = 'CLOSED'


class _RPCState(object):

  def __init__(
      self, call, request_serializer, response_deserializer, sequence_number,
      read, allowance, high_write, low_write, due):
    self.call = call
    self.request_serializer = request_serializer
    self.response_deserializer = response_deserializer
    self.sequence_number = sequence_number
    self.read = read
    self.allowance = allowance
    self.high_write = high_write
    self.low_write = low_write
    self.due = due


def _no_longer_due(kind, rpc_state, key, rpc_states):
  rpc_state.due.remove(kind)
  if not rpc_state.due:
    del rpc_states[key]


class _Kernel(object):

  def __init__(
      self, channel, host, request_serializers, response_deserializers,
      ticket_relay):
    self._lock = threading.Lock()
    self._channel = channel
    self._host = host
    self._request_serializers = request_serializers
    self._response_deserializers = response_deserializers
    self._relay = ticket_relay

    self._completion_queue = None
    self._rpc_states = {}
    self._pool = None

  def _on_write_event(self, operation_id, unused_event, rpc_state):
    if rpc_state.high_write is _HighWrite.CLOSED:
      rpc_state.call.complete(operation_id)
      rpc_state.due.add(_COMPLETE)
      rpc_state.due.remove(_WRITE)
      rpc_state.low_write = _LowWrite.CLOSED
    else:
      ticket = links.Ticket(
          operation_id, rpc_state.sequence_number, None, None, None, None, 1,
          None, None, None, None, None, None, None)
      rpc_state.sequence_number += 1
      self._relay.add_value(ticket)
      rpc_state.low_write = _LowWrite.OPEN
      _no_longer_due(_WRITE, rpc_state, operation_id, self._rpc_states)

  def _on_read_event(self, operation_id, event, rpc_state):
    if event.bytes is None or _FINISH not in rpc_state.due:
      rpc_state.read = _Read.CLOSED
      _no_longer_due(_READ, rpc_state, operation_id, self._rpc_states)
    else:
      if 0 < rpc_state.allowance:
        rpc_state.allowance -= 1
        rpc_state.call.read(operation_id)
      else:
        rpc_state.read = _Read.AWAITING_ALLOWANCE
        _no_longer_due(_READ, rpc_state, operation_id, self._rpc_states)
      ticket = links.Ticket(
          operation_id, rpc_state.sequence_number, None, None, None, None, None,
          None, rpc_state.response_deserializer(event.bytes), None, None, None,
          None, None)
      rpc_state.sequence_number += 1
      self._relay.add_value(ticket)

  def _on_metadata_event(self, operation_id, event, rpc_state):
    if _FINISH in rpc_state.due:
      rpc_state.allowance -= 1
      rpc_state.call.read(operation_id)
      rpc_state.read = _Read.READING
      rpc_state.due.add(_READ)
      rpc_state.due.remove(_METADATA)
      ticket = links.Ticket(
          operation_id, rpc_state.sequence_number, None, None,
          links.Ticket.Subscription.FULL, None, None, event.metadata, None,
          None, None, None, None, None)
      rpc_state.sequence_number += 1
      self._relay.add_value(ticket)
    else:
      _no_longer_due(_METADATA, rpc_state, operation_id, self._rpc_states)

  def _on_finish_event(self, operation_id, event, rpc_state):
    _no_longer_due(_FINISH, rpc_state, operation_id, self._rpc_states)
    if event.status.code is _intermediary_low.Code.OK:
      termination = links.Ticket.Termination.COMPLETION
    elif event.status.code is _intermediary_low.Code.CANCELLED:
      termination = links.Ticket.Termination.CANCELLATION
    elif event.status.code is _intermediary_low.Code.DEADLINE_EXCEEDED:
      termination = links.Ticket.Termination.EXPIRATION
    elif event.status.code is _intermediary_low.Code.UNKNOWN:
      termination = links.Ticket.Termination.LOCAL_FAILURE
    else:
      termination = links.Ticket.Termination.TRANSMISSION_FAILURE
    ticket = links.Ticket(
        operation_id, rpc_state.sequence_number, None, None, None, None, None,
        None, None, event.metadata, event.status.code, event.status.details,
        termination, None)
    rpc_state.sequence_number += 1
    self._relay.add_value(ticket)

  def _spin(self, completion_queue):
    while True:
      event = completion_queue.get(None)
      with self._lock:
        rpc_state = self._rpc_states.get(event.tag, None)
        if event.kind is _STOP:
          pass
        elif event.kind is _WRITE:
          self._on_write_event(event.tag, event, rpc_state)
        elif event.kind is _METADATA:
          self._on_metadata_event(event.tag, event, rpc_state)
        elif event.kind is _READ:
          self._on_read_event(event.tag, event, rpc_state)
        elif event.kind is _FINISH:
          self._on_finish_event(event.tag, event, rpc_state)
        elif event.kind is _COMPLETE:
          _no_longer_due(_COMPLETE, rpc_state, event.tag, self._rpc_states)
        else:
          logging.error('Illegal RPC event! %s', (event,))

        if self._completion_queue is None and not self._rpc_states:
          completion_queue.stop()
          return

  def _invoke(
      self, operation_id, group, method, initial_metadata, payload, termination,
      timeout, allowance):
    """Invoke an RPC.

    Args:
      operation_id: Any object to be used as an operation ID for the RPC.
      group: The group to which the RPC method belongs.
      method: The RPC method name.
      initial_metadata: The initial metadata object for the RPC.
      payload: A payload object for the RPC or None if no payload was given at
        invocation-time.
      termination: A links.Ticket.Termination value or None indicated whether or
        not more writes will follow from this side of the RPC.
      timeout: A duration of time in seconds to allow for the RPC.
      allowance: The number of payloads (beyond the free first one) that the
        local ticket exchange mate has granted permission to be read.
    """
    if termination is links.Ticket.Termination.COMPLETION:
      high_write = _HighWrite.CLOSED
    elif termination is None:
      high_write = _HighWrite.OPEN
    else:
      return

    request_serializer = self._request_serializers.get((group, method))
    response_deserializer = self._response_deserializers.get((group, method))
    if request_serializer is None or response_deserializer is None:
      cancellation_ticket = links.Ticket(
          operation_id, 0, None, None, None, None, None, None, None, None, None,
          None, links.Ticket.Termination.CANCELLATION)
      self._relay.add_value(cancellation_ticket)
      return

    call = _intermediary_low.Call(
        self._channel, self._completion_queue, '/%s/%s' % (group, method),
        self._host, time.time() + timeout)
    if initial_metadata is not None:
      for metadata_key, metadata_value in initial_metadata:
        call.add_metadata(metadata_key, metadata_value)
    call.invoke(self._completion_queue, operation_id, operation_id)
    if payload is None:
      if high_write is _HighWrite.CLOSED:
        call.complete(operation_id)
        low_write = _LowWrite.CLOSED
        due = set((_METADATA, _COMPLETE, _FINISH,))
      else:
        low_write = _LowWrite.OPEN
        due = set((_METADATA, _FINISH,))
    else:
      call.write(request_serializer(payload), operation_id)
      low_write = _LowWrite.ACTIVE
      due = set((_WRITE, _METADATA, _FINISH,))
    self._rpc_states[operation_id] = _RPCState(
        call, request_serializer, response_deserializer, 0,
        _Read.AWAITING_METADATA, 1 if allowance is None else (1 + allowance),
        high_write, low_write, due)

  def _advance(self, operation_id, rpc_state, payload, termination, allowance):
    if payload is not None:
      rpc_state.call.write(rpc_state.request_serializer(payload), operation_id)
      rpc_state.low_write = _LowWrite.ACTIVE
      rpc_state.due.add(_WRITE)

    if allowance is not None:
      if rpc_state.read is _Read.AWAITING_ALLOWANCE:
        rpc_state.allowance += allowance - 1
        rpc_state.call.read(operation_id)
        rpc_state.read = _Read.READING
        rpc_state.due.add(_READ)
      else:
        rpc_state.allowance += allowance

    if termination is links.Ticket.Termination.COMPLETION:
      rpc_state.high_write = _HighWrite.CLOSED
      if rpc_state.low_write is _LowWrite.OPEN:
        rpc_state.call.complete(operation_id)
        rpc_state.due.add(_COMPLETE)
        rpc_state.low_write = _LowWrite.CLOSED
    elif termination is not None:
      rpc_state.call.cancel()

  def add_ticket(self, ticket):
    with self._lock:
      if ticket.sequence_number == 0:
        if self._completion_queue is None:
          logging.error('Received invocation ticket %s after stop!', ticket)
        else:
          self._invoke(
              ticket.operation_id, ticket.group, ticket.method,
              ticket.initial_metadata, ticket.payload, ticket.termination,
              ticket.timeout, ticket.allowance)
      else:
        rpc_state = self._rpc_states.get(ticket.operation_id)
        if rpc_state is not None:
          self._advance(
              ticket.operation_id, rpc_state, ticket.payload,
              ticket.termination, ticket.allowance)

  def start(self):
    """Starts this object.

    This method must be called before attempting to exchange tickets with this
    object.
    """
    with self._lock:
      self._completion_queue = _intermediary_low.CompletionQueue()
      self._pool = logging_pool.pool(1)
      self._pool.submit(self._spin, self._completion_queue)

  def stop(self):
    """Stops this object.

    This method must be called for proper termination of this object, and no
    attempts to exchange tickets with this object may be made after this method
    has been called.
    """
    with self._lock:
      if not self._rpc_states:
        self._completion_queue.stop()
      self._completion_queue = None
      pool = self._pool
    pool.shutdown(wait=True)


class InvocationLink(links.Link, activated.Activated):
  """A links.Link for use on the invocation-side of a gRPC connection.

  Implementations of this interface are only valid for use when activated.
  """
  __metaclass__ = abc.ABCMeta


class _InvocationLink(InvocationLink):

  def __init__(
      self, channel, host, request_serializers, response_deserializers):
    self._relay = relay.relay(None)
    self._kernel = _Kernel(
        channel, host, request_serializers, response_deserializers, self._relay)

  def _start(self):
    self._relay.start()
    self._kernel.start()
    return self

  def _stop(self):
    self._kernel.stop()
    self._relay.stop()

  def accept_ticket(self, ticket):
    """See links.Link.accept_ticket for specification."""
    self._kernel.add_ticket(ticket)

  def join_link(self, link):
    """See links.Link.join_link for specification."""
    self._relay.set_behavior(link.accept_ticket)

  def __enter__(self):
    """See activated.Activated.__enter__ for specification."""
    return self._start()

  def __exit__(self, exc_type, exc_val, exc_tb):
    """See activated.Activated.__exit__ for specification."""
    self._stop()
    return False

  def start(self):
    """See activated.Activated.start for specification."""
    return self._start()

  def stop(self):
    """See activated.Activated.stop for specification."""
    self._stop()


def invocation_link(channel, host, request_serializers, response_deserializers):
  """Creates an InvocationLink.

  Args:
    channel: An _intermediary_low.Channel for use by the link.
    host: The host to specify when invoking RPCs.
    request_serializers: A dict from group-method pair to request object
      serialization behavior.
    response_deserializers: A dict from group-method pair to response object
      deserialization behavior.

  Returns:
    An InvocationLink.
  """
  return _InvocationLink(
      channel, host, request_serializers, response_deserializers)
