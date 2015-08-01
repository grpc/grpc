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

import abc
import enum
import logging
import threading
import time

from grpc._adapter import _intermediary_low
from grpc.framework.foundation import logging_pool
from grpc.framework.foundation import relay
from grpc.framework.interfaces.links import links


@enum.unique
class _Read(enum.Enum):
  READING = 'reading'
  AWAITING_ALLOWANCE = 'awaiting allowance'
  CLOSED = 'closed'


@enum.unique
class _HighWrite(enum.Enum):
  OPEN = 'open'
  CLOSED = 'closed'


@enum.unique
class _LowWrite(enum.Enum):
  """The possible categories of low-level write state."""

  OPEN = 'OPEN'
  ACTIVE = 'ACTIVE'
  CLOSED = 'CLOSED'


class _RPCState(object):

  def __init__(
      self, request_deserializer, response_serializer, sequence_number, read,
      allowance, high_write, low_write, premetadataed, terminal_metadata, code,
      message):
    self.request_deserializer = request_deserializer
    self.response_serializer = response_serializer
    self.sequence_number = sequence_number
    self.read = read
    self.allowance = allowance
    self.high_write = high_write
    self.low_write = low_write
    self.premetadataed = premetadataed
    self.terminal_metadata = terminal_metadata
    self.code = code
    self.message = message


def _metadatafy(call, metadata):
  for metadata_key, metadata_value in metadata:
    call.add_metadata(metadata_key, metadata_value)


class _Kernel(object):

  def __init__(self, request_deserializers, response_serializers, ticket_relay):
    self._lock = threading.Lock()
    self._request_deserializers = request_deserializers
    self._response_serializers = response_serializers
    self._relay = ticket_relay

    self._completion_queue = None
    self._server = None
    self._rpc_states = {}
    self._pool = None

  def _on_service_acceptance_event(self, event, server):
    server.service(None)

    service_acceptance = event.service_acceptance
    call = service_acceptance.call
    call.accept(self._completion_queue, call)
    try:
      group, method = service_acceptance.method.split('/')[1:3]
    except ValueError:
      logging.info('Illegal path "%s"!', service_acceptance.method)
      return
    request_deserializer = self._request_deserializers.get((group, method))
    response_serializer = self._response_serializers.get((group, method))
    if request_deserializer is None or response_serializer is None:
      # TODO(nathaniel): Terminate the RPC with code NOT_FOUND.
      call.cancel()
      return

    call.read(call)
    self._rpc_states[call] = _RPCState(
        request_deserializer, response_serializer, 1, _Read.READING, 0,
        _HighWrite.OPEN, _LowWrite.OPEN, False, None, None, None)
    ticket = links.Ticket(
        call, 0, group, method, links.Ticket.Subscription.FULL,
        service_acceptance.deadline - time.time(), None, event.metadata, None,
        None, None, None, None)
    self._relay.add_value(ticket)

  def _on_read_event(self, event):
    call = event.tag
    rpc_state = self._rpc_states.get(call, None)
    if rpc_state is None:
      return

    if event.bytes is None:
      rpc_state.read = _Read.CLOSED
      payload = None
      termination = links.Ticket.Termination.COMPLETION
    else:
      if 0 < rpc_state.allowance:
        rpc_state.allowance -= 1
        call.read(call)
      else:
        rpc_state.read = _Read.AWAITING_ALLOWANCE
      payload = rpc_state.request_deserializer(event.bytes)
      termination = None
    ticket = links.Ticket(
        call, rpc_state.sequence_number, None, None, None, None, None, None,
        payload, None, None, None, termination)
    rpc_state.sequence_number += 1
    self._relay.add_value(ticket)

  def _on_write_event(self, event):
    call = event.tag
    rpc_state = self._rpc_states.get(call, None)
    if rpc_state is None:
      return

    if rpc_state.high_write is _HighWrite.CLOSED:
      if rpc_state.terminal_metadata is not None:
        _metadatafy(call, rpc_state.terminal_metadata)
      call.status(
          _intermediary_low.Status(rpc_state.code, rpc_state.message), call)
      rpc_state.low_write = _LowWrite.CLOSED
    else:
      ticket = links.Ticket(
          call, rpc_state.sequence_number, None, None, None, None, 1, None,
          None, None, None, None, None)
      rpc_state.sequence_number += 1
      self._relay.add_value(ticket)
      rpc_state.low_write = _LowWrite.OPEN

  def _on_finish_event(self, event):
    call = event.tag
    rpc_state = self._rpc_states.pop(call, None)
    if rpc_state is None:
      return
    code = event.status.code
    if code is _intermediary_low.Code.OK:
      return

    if code is _intermediary_low.Code.CANCELLED:
      termination = links.Ticket.Termination.CANCELLATION
    elif code is _intermediary_low.Code.DEADLINE_EXCEEDED:
      termination = links.Ticket.Termination.EXPIRATION
    else:
      termination = links.Ticket.Termination.TRANSMISSION_FAILURE
    ticket = links.Ticket(
        call, rpc_state.sequence_number, None, None, None, None, None, None,
        None, None, None, None, termination)
    rpc_state.sequence_number += 1
    self._relay.add_value(ticket)

  def _spin(self, completion_queue, server):
    while True:
      event = completion_queue.get(None)
      if event.kind is _intermediary_low.Event.Kind.STOP:
        return
      with self._lock:
        if self._server is None:
          continue
        elif event.kind is _intermediary_low.Event.Kind.SERVICE_ACCEPTED:
          self._on_service_acceptance_event(event, server)
        elif event.kind is _intermediary_low.Event.Kind.READ_ACCEPTED:
          self._on_read_event(event)
        elif event.kind is _intermediary_low.Event.Kind.WRITE_ACCEPTED:
          self._on_write_event(event)
        elif event.kind is _intermediary_low.Event.Kind.COMPLETE_ACCEPTED:
          pass
        elif event.kind is _intermediary_low.Event.Kind.FINISH:
          self._on_finish_event(event)
        else:
          logging.error('Illegal event! %s', (event,))

  def add_ticket(self, ticket):
    with self._lock:
      if self._server is None:
        return
      call = ticket.operation_id
      rpc_state = self._rpc_states.get(call)
      if rpc_state is None:
        return

      if ticket.initial_metadata is not None:
        _metadatafy(call, ticket.initial_metadata)
        call.premetadata()
        rpc_state.premetadataed = True
      elif not rpc_state.premetadataed:
        if (ticket.terminal_metadata is not None or
            ticket.payload is not None or
            ticket.termination is links.Ticket.Termination.COMPLETION or
            ticket.code is not None or
            ticket.message is not None):
          call.premetadata()
          rpc_state.premetadataed = True

      if ticket.allowance is not None:
        if rpc_state.read is _Read.AWAITING_ALLOWANCE:
          rpc_state.allowance += ticket.allowance - 1
          call.read(call)
          rpc_state.read = _Read.READING
        else:
          rpc_state.allowance += ticket.allowance

      if ticket.payload is not None:
        call.write(rpc_state.response_serializer(ticket.payload), call)
        rpc_state.low_write = _LowWrite.ACTIVE

      if ticket.terminal_metadata is not None:
        rpc_state.terminal_metadata = ticket.terminal_metadata
      if ticket.code is not None:
        rpc_state.code = ticket.code
      if ticket.message is not None:
        rpc_state.message = ticket.message

      if ticket.termination is links.Ticket.Termination.COMPLETION:
        rpc_state.high_write = _HighWrite.CLOSED
        if rpc_state.low_write is _LowWrite.OPEN:
          if rpc_state.terminal_metadata is not None:
            _metadatafy(call, rpc_state.terminal_metadata)
          status = _intermediary_low.Status(
              _intermediary_low.Code.OK
              if rpc_state.code is None else rpc_state.code,
              '' if rpc_state.message is None else rpc_state.message)
          call.status(status, call)
          rpc_state.low_write = _LowWrite.CLOSED
      elif ticket.termination is not None:
        call.cancel()
        self._rpc_states.pop(call, None)

  def add_port(self, port, server_credentials):
    with self._lock:
      address = '[::]:%d' % port
      if self._server is None:
        self._completion_queue = _intermediary_low.CompletionQueue()
        self._server = _intermediary_low.Server(self._completion_queue)
      if server_credentials is None:
        return self._server.add_http2_addr(address)
      else:
        return self._server.add_secure_http2_addr(address, server_credentials)

  def start(self):
    with self._lock:
      if self._server is None:
        self._completion_queue = _intermediary_low.CompletionQueue()
        self._server = _intermediary_low.Server(self._completion_queue)
      self._pool = logging_pool.pool(1)
      self._pool.submit(self._spin, self._completion_queue, self._server)
      self._server.start()
      self._server.service(None)

  def graceful_stop(self):
    with self._lock:
      self._server.stop()
      self._server = None
      self._completion_queue.stop()
      self._completion_queue = None
      pool = self._pool
      self._pool = None
      self._rpc_states = None
    pool.shutdown(wait=True)

  def immediate_stop(self):
    # TODO(nathaniel): Implementation.
    raise NotImplementedError(
        'TODO(nathaniel): after merge of rewritten lower layers')


class ServiceLink(links.Link):
  """A links.Link for use on the service-side of a gRPC connection.

  Implementations of this interface are only valid for use between calls to
  their start method and one of their stop methods.
  """

  @abc.abstractmethod
  def add_port(self, port, server_credentials):
    """Adds a port on which to service RPCs after this link has been started.

    Args:
      port: The port on which to service RPCs, or zero to request that a port be
        automatically selected and used.
      server_credentials: A ServerCredentials object, or None for insecure
        service.

    Returns:
      A port on which RPCs will be serviced after this link has been started.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def start(self):
    """Starts this object.

    This method must be called before attempting to use this Link in ticket
    exchange.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stop_gracefully(self):
    """Stops this link.

    New RPCs will be rejected as soon as this method is called, but ongoing RPCs
    will be allowed to continue until they terminate. This method blocks until
    all RPCs have terminated.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stop_immediately(self):
    """Stops this link.

    All in-progress RPCs will be terminated immediately.
    """
    raise NotImplementedError()


class _ServiceLink(ServiceLink):

  def __init__(self, request_deserializers, response_serializers):
    self._relay = relay.relay(None)
    self._kernel = _Kernel(
        request_deserializers, response_serializers, self._relay)

  def accept_ticket(self, ticket):
    self._kernel.add_ticket(ticket)

  def join_link(self, link):
    self._relay.set_behavior(link.accept_ticket)

  def add_port(self, port, server_credentials):
    return self._kernel.add_port(port, server_credentials)

  def start(self):
    self._relay.start()
    return self._kernel.start()

  def stop_gracefully(self):
    self._kernel.graceful_stop()
    self._relay.stop()

  def stop_immediately(self):
    self._kernel.immediate_stop()
    self._relay.stop()


def service_link(request_deserializers, response_serializers):
  """Creates a ServiceLink.

  Args:
    request_deserializers: A dict from group-method pair to request object
      deserialization behavior.
    response_serializers: A dict from group-method pair to response ojbect
      serialization behavior.

  Returns:
    A ServiceLink.
  """
  return _ServiceLink(request_deserializers, response_serializers)
