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

"""The RPC-service-side bridge between RPC Framework and GRPC-on-the-wire."""

import enum
import logging
import threading
import time

from grpc._adapter import _common
from grpc._adapter import _intermediary_low as _low
from grpc.framework.base import interfaces as base_interfaces
from grpc.framework.base import null
from grpc.framework.foundation import activated
from grpc.framework.foundation import logging_pool

_THREAD_POOL_SIZE = 10


@enum.unique
class _LowWrite(enum.Enum):
  """The possible categories of low-level write state."""

  OPEN = 'OPEN'
  ACTIVE = 'ACTIVE'
  CLOSED = 'CLOSED'


def _write(call, rpc_state, payload):
  serialized_payload = rpc_state.serializer(payload)
  if rpc_state.write.low is _LowWrite.OPEN:
    call.write(serialized_payload, call, 0)
    rpc_state.write.low = _LowWrite.ACTIVE
  else:
    rpc_state.write.pending.append(serialized_payload)


def _status(call, rpc_state):
  call.status(_low.Status(_low.Code.OK, ''), call)
  rpc_state.write.low = _LowWrite.CLOSED


class ForeLink(base_interfaces.ForeLink, activated.Activated):
  """A service-side bridge between RPC Framework and the C-ish _low code."""

  def __init__(
      self, pool, request_deserializers, response_serializers,
      root_certificates, key_chain_pairs, port=None):
    """Constructor.

    Args:
      pool: A thread pool.
      request_deserializers: A dict from RPC method names to request object
        deserializer behaviors.
      response_serializers: A dict from RPC method names to response object
        serializer behaviors.
      root_certificates: The PEM-encoded client root certificates as a
        bytestring or None.
      key_chain_pairs: A sequence of PEM-encoded private key-certificate chain
        pairs.
      port: The port on which to serve, or None to have a port selected
        automatically.
    """
    self._condition = threading.Condition()
    self._pool = pool
    self._request_deserializers = request_deserializers
    self._response_serializers = response_serializers
    self._root_certificates = root_certificates
    self._key_chain_pairs = key_chain_pairs
    self._requested_port = port

    self._rear_link = null.NULL_REAR_LINK
    self._completion_queue = None
    self._server = None
    self._rpc_states = {}
    self._spinning = False
    self._port = None

  def _on_stop_event(self):
    self._spinning = False
    self._condition.notify_all()

  def _on_service_acceptance_event(self, event, server):
    """Handle a service invocation event."""
    service_acceptance = event.service_acceptance
    if service_acceptance is None:
      return

    call = service_acceptance.call
    call.accept(self._completion_queue, call)
    # TODO(nathaniel): Metadata support.
    call.premetadata()
    call.read(call)
    method = service_acceptance.method

    self._rpc_states[call] = _common.CommonRPCState(
        _common.WriteState(_LowWrite.OPEN, _common.HighWrite.OPEN, []), 1,
        self._request_deserializers[method],
        self._response_serializers[method])

    ticket = base_interfaces.FrontToBackTicket(
        call, 0, base_interfaces.FrontToBackTicket.Kind.COMMENCEMENT, method,
        base_interfaces.ServicedSubscription.Kind.FULL, None, None,
        service_acceptance.deadline - time.time())
    self._rear_link.accept_front_to_back_ticket(ticket)

    server.service(None)

  def _on_read_event(self, event):
    """Handle data arriving during an RPC."""
    call = event.tag
    rpc_state = self._rpc_states.get(call, None)
    if rpc_state is None:
      return

    sequence_number = rpc_state.sequence_number
    rpc_state.sequence_number += 1
    if event.bytes is None:
      ticket = base_interfaces.FrontToBackTicket(
          call, sequence_number,
          base_interfaces.FrontToBackTicket.Kind.COMPLETION, None, None, None,
          None, None)
    else:
      call.read(call)
      ticket = base_interfaces.FrontToBackTicket(
          call, sequence_number,
          base_interfaces.FrontToBackTicket.Kind.CONTINUATION, None, None,
          None, rpc_state.deserializer(event.bytes), None)

    self._rear_link.accept_front_to_back_ticket(ticket)

  def _on_write_event(self, event):
    call = event.tag
    rpc_state = self._rpc_states.get(call, None)
    if rpc_state is None:
      return

    if rpc_state.write.pending:
      serialized_payload = rpc_state.write.pending.pop(0)
      call.write(serialized_payload, call, 0)
    elif rpc_state.write.high is _common.HighWrite.CLOSED:
      _status(call, rpc_state)
    else:
      rpc_state.write.low = _LowWrite.OPEN

  def _on_complete_event(self, event):
    if not event.complete_accepted:
      logging.error('Complete not accepted! %s', (event,))
      call = event.tag
      rpc_state = self._rpc_states.pop(call, None)
      if rpc_state is None:
        return

      sequence_number = rpc_state.sequence_number
      rpc_state.sequence_number += 1
      ticket = base_interfaces.FrontToBackTicket(
          call, sequence_number,
          base_interfaces.FrontToBackTicket.Kind.TRANSMISSION_FAILURE, None,
          None, None, None, None)
      self._rear_link.accept_front_to_back_ticket(ticket)

  def _on_finish_event(self, event):
    """Handle termination of an RPC."""
    call = event.tag
    rpc_state = self._rpc_states.pop(call, None)
    if rpc_state is None:
      return

    code = event.status.code
    if code is _low.Code.OK:
      return

    sequence_number = rpc_state.sequence_number
    rpc_state.sequence_number += 1
    if code is _low.Code.CANCELLED:
      ticket = base_interfaces.FrontToBackTicket(
          call, sequence_number,
          base_interfaces.FrontToBackTicket.Kind.CANCELLATION, None, None,
          None, None, None)
    elif code is _low.Code.DEADLINE_EXCEEDED:
      ticket = base_interfaces.FrontToBackTicket(
          call, sequence_number,
          base_interfaces.FrontToBackTicket.Kind.EXPIRATION, None, None, None,
          None, None)
    else:
      # TODO(nathaniel): Better mapping of codes to ticket-categories
      ticket = base_interfaces.FrontToBackTicket(
          call, sequence_number,
          base_interfaces.FrontToBackTicket.Kind.TRANSMISSION_FAILURE, None,
          None, None, None, None)
    self._rear_link.accept_front_to_back_ticket(ticket)

  def _spin(self, completion_queue, server):
    while True:
      event = completion_queue.get(None)

      with self._condition:
        if event.kind is _low.Event.Kind.STOP:
          self._on_stop_event()
          return
        elif self._server is None:
          continue
        elif event.kind is _low.Event.Kind.SERVICE_ACCEPTED:
          self._on_service_acceptance_event(event, server)
        elif event.kind is _low.Event.Kind.READ_ACCEPTED:
          self._on_read_event(event)
        elif event.kind is _low.Event.Kind.WRITE_ACCEPTED:
          self._on_write_event(event)
        elif event.kind is _low.Event.Kind.COMPLETE_ACCEPTED:
          self._on_complete_event(event)
        elif event.kind is _low.Event.Kind.FINISH:
          self._on_finish_event(event)
        else:
          logging.error('Illegal event! %s', (event,))

  def _continue(self, call, payload):
    rpc_state = self._rpc_states.get(call, None)
    if rpc_state is None:
      return

    _write(call, rpc_state, payload)

  def _complete(self, call, payload):
    """Handle completion of the writes of an RPC."""
    rpc_state = self._rpc_states.get(call, None)
    if rpc_state is None:
      return

    if rpc_state.write.low is _LowWrite.OPEN:
      if payload is None:
        _status(call, rpc_state)
      else:
        _write(call, rpc_state, payload)
    elif rpc_state.write.low is _LowWrite.ACTIVE:
      if payload is not None:
        rpc_state.write.pending.append(rpc_state.serializer(payload))
    else:
      raise ValueError('Called to complete after having already completed!')
    rpc_state.write.high = _common.HighWrite.CLOSED

  def _cancel(self, call):
    call.cancel()
    self._rpc_states.pop(call, None)

  def join_rear_link(self, rear_link):
    """See base_interfaces.ForeLink.join_rear_link for specification."""
    self._rear_link = null.NULL_REAR_LINK if rear_link is None else rear_link

  def _start(self):
    """Starts this ForeLink.

    This method must be called before attempting to exchange tickets with this
    object.
    """
    with self._condition:
      address = '[::]:%d' % (
          0 if self._requested_port is None else self._requested_port)
      self._completion_queue = _low.CompletionQueue()
      if self._root_certificates is None and not self._key_chain_pairs:
        self._server = _low.Server(self._completion_queue)
        self._port = self._server.add_http2_addr(address)
      else:
        server_credentials = _low.ServerCredentials(
          self._root_certificates, self._key_chain_pairs, False)
        self._server = _low.Server(self._completion_queue)
        self._port = self._server.add_secure_http2_addr(
            address, server_credentials)
      self._server.start()

      self._server.service(None)

      self._pool.submit(self._spin, self._completion_queue, self._server)
      self._spinning = True

      return self

  # TODO(nathaniel): Expose graceful-shutdown semantics in which this object
  # enters a state in which it finishes ongoing RPCs but refuses new ones.
  def _stop(self):
    """Stops this ForeLink.

    This method must be called for proper termination of this object, and no
    attempts to exchange tickets with this object may be made after this method
    has been called.
    """
    with self._condition:
      self._server.stop()
      # TODO(nathaniel): Yep, this is weird. Deleting a server shouldn't have a
      # behaviorally significant side-effect.
      self._server = None
      self._completion_queue.stop()

      while self._spinning:
        self._condition.wait()

      self._port = None

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

  def port(self):
    """Identifies the port on which this ForeLink is servicing RPCs.

    Returns:
      The number of the port on which this ForeLink is servicing RPCs, or None
        if this ForeLink is not currently activated and servicing RPCs.
    """
    with self._condition:
      return self._port

  def accept_back_to_front_ticket(self, ticket):
    """See base_interfaces.ForeLink.accept_back_to_front_ticket for spec."""
    with self._condition:
      if self._server is None:
        return

      if ticket.kind is base_interfaces.BackToFrontTicket.Kind.CONTINUATION:
        self._continue(ticket.operation_id, ticket.payload)
      elif ticket.kind is base_interfaces.BackToFrontTicket.Kind.COMPLETION:
        self._complete(ticket.operation_id, ticket.payload)
      else:
        self._cancel(ticket.operation_id)
