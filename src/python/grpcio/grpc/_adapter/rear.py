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

_INVOCATION_EVENT_KINDS = (
    _low.Event.Kind.METADATA_ACCEPTED,
    _low.Event.Kind.FINISH
)


@enum.unique
class _LowWrite(enum.Enum):
  """The possible categories of low-level write state."""

  OPEN = 'OPEN'
  ACTIVE = 'ACTIVE'
  CLOSED = 'CLOSED'


class _RPCState(object):
  """The full state of any tracked RPC.

  Attributes:
    call: The _low.Call object for the RPC.
    outstanding: The set of Event.Kind values describing expected future events
      for the RPC.
    active: A boolean indicating whether or not the RPC is active.
    common: An _common.RPCState describing additional state for the RPC.
  """

  def __init__(self, call, outstanding, active, common):
    self.call = call
    self.outstanding = outstanding
    self.active = active
    self.common = common


def _write(operation_id, call, outstanding, write_state, serialized_payload):
  if write_state.low is _LowWrite.OPEN:
    call.write(serialized_payload, operation_id, 0)
    outstanding.add(_low.Event.Kind.WRITE_ACCEPTED)
    write_state.low = _LowWrite.ACTIVE
  elif write_state.low is _LowWrite.ACTIVE:
    write_state.pending.append(serialized_payload)
  else:
    raise ValueError('Write attempted after writes completed!')


class RearLink(base_interfaces.RearLink, activated.Activated):
  """An invocation-side bridge between RPC Framework and the C-ish _low code."""

  def __init__(
      self, host, port, pool, request_serializers, response_deserializers,
      secure, root_certificates, private_key, certificate_chain,
      metadata_transformer=None, server_host_override=None):
    """Constructor.

    Args:
      host: The host to which to connect for RPC service.
      port: The port to which to connect for RPC service.
      pool: A thread pool.
      request_serializers: A dict from RPC method names to request object
        serializer behaviors.
      response_deserializers: A dict from RPC method names to response object
        deserializer behaviors.
      secure: A boolean indicating whether or not to use a secure connection.
      root_certificates: The PEM-encoded root certificates or None to ask for
        them to be retrieved from a default location.
      private_key: The PEM-encoded private key to use or None if no private
        key should be used.
      certificate_chain: The PEM-encoded certificate chain to use or None if
        no certificate chain should be used.
      metadata_transformer: A function that given a metadata object produces
        another metadata to be used in the underlying communication on the
        wire.
      server_host_override: (For testing only) the target name used for SSL
        host name checking.
    """
    self._condition = threading.Condition()
    self._host = host
    self._port = port
    self._pool = pool
    self._request_serializers = request_serializers
    self._response_deserializers = response_deserializers

    self._fore_link = null.NULL_FORE_LINK
    self._completion_queue = None
    self._channel = None
    self._rpc_states = {}
    self._spinning = False
    if secure:
      self._client_credentials = _low.ClientCredentials(
          root_certificates, private_key, certificate_chain)
    else:
      self._client_credentials = None
    self._root_certificates = root_certificates
    self._private_key = private_key
    self._certificate_chain = certificate_chain
    self._metadata_transformer = metadata_transformer
    self._server_host_override = server_host_override

  def _on_write_event(self, operation_id, event, rpc_state):
    if event.write_accepted:
      if rpc_state.common.write.pending:
        rpc_state.call.write(
            rpc_state.common.write.pending.pop(0), operation_id, 0)
        rpc_state.outstanding.add(_low.Event.Kind.WRITE_ACCEPTED)
      elif rpc_state.common.write.high is _common.HighWrite.CLOSED:
        rpc_state.call.complete(operation_id)
        rpc_state.outstanding.add(_low.Event.Kind.COMPLETE_ACCEPTED)
        rpc_state.common.write.low = _LowWrite.CLOSED
      else:
        rpc_state.common.write.low = _LowWrite.OPEN
    else:
      logging.error('RPC write not accepted! Event: %s', (event,))
      rpc_state.active = False
      ticket = base_interfaces.BackToFrontTicket(
          operation_id, rpc_state.common.sequence_number,
          base_interfaces.BackToFrontTicket.Kind.TRANSMISSION_FAILURE, None)
      rpc_state.common.sequence_number += 1
      self._fore_link.accept_back_to_front_ticket(ticket)

  def _on_read_event(self, operation_id, event, rpc_state):
    if event.bytes is not None:
      rpc_state.call.read(operation_id)
      rpc_state.outstanding.add(_low.Event.Kind.READ_ACCEPTED)

      ticket = base_interfaces.BackToFrontTicket(
          operation_id, rpc_state.common.sequence_number,
          base_interfaces.BackToFrontTicket.Kind.CONTINUATION,
          rpc_state.common.deserializer(event.bytes))
      rpc_state.common.sequence_number += 1
      self._fore_link.accept_back_to_front_ticket(ticket)

  def _on_complete_event(self, operation_id, event, rpc_state):
    if not event.complete_accepted:
      logging.error('RPC complete not accepted! Event: %s', (event,))
      rpc_state.active = False
      ticket = base_interfaces.BackToFrontTicket(
          operation_id, rpc_state.common.sequence_number,
          base_interfaces.BackToFrontTicket.Kind.TRANSMISSION_FAILURE, None)
      rpc_state.common.sequence_number += 1
      self._fore_link.accept_back_to_front_ticket(ticket)

  # TODO(nathaniel): Metadata support.
  def _on_metadata_event(self, operation_id, event, rpc_state):  # pylint: disable=unused-argument
    rpc_state.call.read(operation_id)
    rpc_state.outstanding.add(_low.Event.Kind.READ_ACCEPTED)

  def _on_finish_event(self, operation_id, event, rpc_state):
    """Handle termination of an RPC."""
    # TODO(nathaniel): Cover all statuses.
    if event.status.code is _low.Code.OK:
      kind = base_interfaces.BackToFrontTicket.Kind.COMPLETION
    elif event.status.code is _low.Code.CANCELLED:
      kind = base_interfaces.BackToFrontTicket.Kind.CANCELLATION
    elif event.status.code is _low.Code.DEADLINE_EXCEEDED:
      kind = base_interfaces.BackToFrontTicket.Kind.EXPIRATION
    else:
      kind = base_interfaces.BackToFrontTicket.Kind.TRANSMISSION_FAILURE
    ticket = base_interfaces.BackToFrontTicket(
        operation_id, rpc_state.common.sequence_number, kind, None)
    rpc_state.common.sequence_number += 1
    self._fore_link.accept_back_to_front_ticket(ticket)

  def _spin(self, completion_queue):
    while True:
      event = completion_queue.get(None)
      operation_id = event.tag

      with self._condition:
        rpc_state = self._rpc_states[operation_id]
        rpc_state.outstanding.remove(event.kind)
        if rpc_state.active and self._completion_queue is not None:
          if event.kind is _low.Event.Kind.WRITE_ACCEPTED:
            self._on_write_event(operation_id, event, rpc_state)
          elif event.kind is _low.Event.Kind.METADATA_ACCEPTED:
            self._on_metadata_event(operation_id, event, rpc_state)
          elif event.kind is _low.Event.Kind.READ_ACCEPTED:
            self._on_read_event(operation_id, event, rpc_state)
          elif event.kind is _low.Event.Kind.COMPLETE_ACCEPTED:
            self._on_complete_event(operation_id, event, rpc_state)
          elif event.kind is _low.Event.Kind.FINISH:
            self._on_finish_event(operation_id, event, rpc_state)
          else:
            logging.error('Illegal RPC event! %s', (event,))

        if not rpc_state.outstanding:
          self._rpc_states.pop(operation_id)
        if not self._rpc_states:
          self._spinning = False
          self._condition.notify_all()
          return

  def _invoke(self, operation_id, name, high_state, payload, timeout):
    """Invoke an RPC.

    Args:
      operation_id: Any object to be used as an operation ID for the RPC.
      name: The RPC method name.
      high_state: A _common.HighWrite value representing the "high write state"
        of the RPC.
      payload: A payload object for the RPC or None if no payload was given at
        invocation-time.
      timeout: A duration of time in seconds to allow for the RPC.
    """
    request_serializer = self._request_serializers[name]
    call = _low.Call(self._channel, self._completion_queue, name, self._host, time.time() + timeout)
    if self._metadata_transformer is not None:
      metadata = self._metadata_transformer([])
      for metadata_key, metadata_value in metadata:
        call.add_metadata(metadata_key, metadata_value)
    call.invoke(self._completion_queue, operation_id, operation_id)
    outstanding = set(_INVOCATION_EVENT_KINDS)

    if payload is None:
      if high_state is _common.HighWrite.CLOSED:
        call.complete(operation_id)
        low_state = _LowWrite.CLOSED
        outstanding.add(_low.Event.Kind.COMPLETE_ACCEPTED)
      else:
        low_state = _LowWrite.OPEN
    else:
      serialized_payload = request_serializer(payload)
      call.write(serialized_payload, operation_id, 0)
      outstanding.add(_low.Event.Kind.WRITE_ACCEPTED)
      low_state = _LowWrite.ACTIVE

    write_state = _common.WriteState(low_state, high_state, [])
    common_state = _common.CommonRPCState(
        write_state, 0, self._response_deserializers[name], request_serializer)
    self._rpc_states[operation_id] = _RPCState(
        call, outstanding, True, common_state)

    if not self._spinning:
      self._pool.submit(self._spin, self._completion_queue)
      self._spinning = True

  def _commence(self, operation_id, name, payload, timeout):
    self._invoke(operation_id, name, _common.HighWrite.OPEN, payload, timeout)

  def _continue(self, operation_id, payload):
    rpc_state = self._rpc_states.get(operation_id, None)
    if rpc_state is None or not rpc_state.active:
      return

    _write(
        operation_id, rpc_state.call, rpc_state.outstanding,
        rpc_state.common.write, rpc_state.common.serializer(payload))

  def _complete(self, operation_id, payload):
    """Close writes associated with an ongoing RPC.

    Args:
      operation_id: Any object being use as an operation ID for the RPC.
      payload: A payload object for the RPC (and thus the last payload object
        for the RPC) or None if no payload was given along with the instruction
        to indicate the end of writes for the RPC.
    """
    rpc_state = self._rpc_states.get(operation_id, None)
    if rpc_state is None or not rpc_state.active:
      return

    write_state = rpc_state.common.write
    if payload is None:
      if write_state.low is _LowWrite.OPEN:
        rpc_state.call.complete(operation_id)
        rpc_state.outstanding.add(_low.Event.Kind.COMPLETE_ACCEPTED)
        write_state.low = _LowWrite.CLOSED
    else:
      _write(
          operation_id, rpc_state.call, rpc_state.outstanding, write_state,
          rpc_state.common.serializer(payload))
    write_state.high = _common.HighWrite.CLOSED

  def _entire(self, operation_id, name, payload, timeout):
    self._invoke(operation_id, name, _common.HighWrite.CLOSED, payload, timeout)

  def _cancel(self, operation_id):
    rpc_state = self._rpc_states.get(operation_id, None)
    if rpc_state is not None and rpc_state.active:
      rpc_state.call.cancel()
      rpc_state.active = False

  def join_fore_link(self, fore_link):
    """See base_interfaces.RearLink.join_fore_link for specification."""
    with self._condition:
      self._fore_link = null.NULL_FORE_LINK if fore_link is None else fore_link

  def _start(self):
    """Starts this RearLink.

    This method must be called before attempting to exchange tickets with this
    object.
    """
    with self._condition:
      self._completion_queue = _low.CompletionQueue()
      self._channel = _low.Channel(
          '%s:%d' % (self._host, self._port), self._client_credentials,
          server_host_override=self._server_host_override)
    return self

  def _stop(self):
    """Stops this RearLink.

    This method must be called for proper termination of this object, and no
    attempts to exchange tickets with this object may be made after this method
    has been called.
    """
    with self._condition:
      self._completion_queue.stop()
      self._completion_queue = None

      while self._spinning:
        self._condition.wait()

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

  def accept_front_to_back_ticket(self, ticket):
    """See base_interfaces.RearLink.accept_front_to_back_ticket for spec."""
    with self._condition:
      if self._completion_queue is None:
        return

      if ticket.kind is base_interfaces.FrontToBackTicket.Kind.COMMENCEMENT:
        self._commence(
            ticket.operation_id, ticket.name, ticket.payload, ticket.timeout)
      elif ticket.kind is base_interfaces.FrontToBackTicket.Kind.CONTINUATION:
        self._continue(ticket.operation_id, ticket.payload)
      elif ticket.kind is base_interfaces.FrontToBackTicket.Kind.COMPLETION:
        self._complete(ticket.operation_id, ticket.payload)
      elif ticket.kind is base_interfaces.FrontToBackTicket.Kind.ENTIRE:
        self._entire(
            ticket.operation_id, ticket.name, ticket.payload, ticket.timeout)
      elif ticket.kind is base_interfaces.FrontToBackTicket.Kind.CANCELLATION:
        self._cancel(ticket.operation_id)
      else:
        # NOTE(nathaniel): All other categories are treated as cancellation.
        self._cancel(ticket.operation_id)
