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
from grpc._links import _constants
from grpc.beta import interfaces as beta_interfaces
from grpc.framework.foundation import logging_pool
from grpc.framework.foundation import relay
from grpc.framework.interfaces.links import links

_IDENTITY = lambda x: x

_TERMINATION_KIND_TO_CODE = {
    links.Ticket.Termination.COMPLETION: _intermediary_low.Code.OK,
    links.Ticket.Termination.CANCELLATION: _intermediary_low.Code.CANCELLED,
    links.Ticket.Termination.EXPIRATION:
        _intermediary_low.Code.DEADLINE_EXCEEDED,
    links.Ticket.Termination.SHUTDOWN: _intermediary_low.Code.UNAVAILABLE,
    links.Ticket.Termination.RECEPTION_FAILURE: _intermediary_low.Code.INTERNAL,
    links.Ticket.Termination.TRANSMISSION_FAILURE:
        _intermediary_low.Code.INTERNAL,
    links.Ticket.Termination.LOCAL_FAILURE: _intermediary_low.Code.UNKNOWN,
    links.Ticket.Termination.REMOTE_FAILURE: _intermediary_low.Code.UNKNOWN,
}

_STOP = _intermediary_low.Event.Kind.STOP
_WRITE = _intermediary_low.Event.Kind.WRITE_ACCEPTED
_COMPLETE = _intermediary_low.Event.Kind.COMPLETE_ACCEPTED
_SERVICE = _intermediary_low.Event.Kind.SERVICE_ACCEPTED
_READ = _intermediary_low.Event.Kind.READ_ACCEPTED
_FINISH = _intermediary_low.Event.Kind.FINISH


@enum.unique
class _Read(enum.Enum):
  READING = 'reading'
  # TODO(issue 2916): This state will again be necessary after eliminating the
  # "early_read" field of _RPCState and going back to only reading when granted
  # allowance to read.
  # AWAITING_ALLOWANCE = 'awaiting allowance'
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


class _Context(beta_interfaces.GRPCServicerContext):

  def __init__(self, call):
    self._lock = threading.Lock()
    self._call = call
    self._disable_next_compression = False

  def peer(self):
    with self._lock:
      return self._call.peer()

  def disable_next_response_compression(self):
    with self._lock:
      self._disable_next_compression = True

  def next_compression_disabled(self):
    with self._lock:
      disabled = self._disable_next_compression
      self._disable_next_compression = False
      return disabled


class _RPCState(object):

  def __init__(
      self, request_deserializer, response_serializer, sequence_number, read,
      early_read, allowance, high_write, low_write, premetadataed,
      terminal_metadata, code, message, due, context):
    self.request_deserializer = request_deserializer
    self.response_serializer = response_serializer
    self.sequence_number = sequence_number
    self.read = read
    # TODO(issue 2916): Eliminate this by eliminating the necessity of calling
    # call.read just to advance the RPC.
    self.early_read = early_read  # A raw (not deserialized) read.
    self.allowance = allowance
    self.high_write = high_write
    self.low_write = low_write
    self.premetadataed = premetadataed
    self.terminal_metadata = terminal_metadata
    self.code = code
    self.message = message
    self.due = due
    self.context = context


def _no_longer_due(kind, rpc_state, key, rpc_states):
  rpc_state.due.remove(kind)
  if not rpc_state.due:
    del rpc_states[key]


def _metadatafy(call, metadata):
  for metadata_key, metadata_value in metadata:
    call.add_metadata(metadata_key, metadata_value)


def _status(termination_kind, high_code, details):
  low_details = b'' if details is None else details
  if high_code is None:
    low_code = _TERMINATION_KIND_TO_CODE[termination_kind]
  else:
    low_code = _constants.HIGH_STATUS_CODE_TO_LOW_STATUS_CODE[high_code]
  return _intermediary_low.Status(low_code, low_details)


class _Kernel(object):

  def __init__(self, request_deserializers, response_serializers, ticket_relay):
    self._lock = threading.Lock()
    self._request_deserializers = request_deserializers
    self._response_serializers = response_serializers
    self._relay = ticket_relay

    self._completion_queue = None
    self._due = set()
    self._server = None
    self._rpc_states = {}
    self._pool = None

  def _on_service_acceptance_event(self, event, server):
    server.service(None)

    service_acceptance = event.service_acceptance
    call = service_acceptance.call
    call.accept(self._completion_queue, call)
    try:
      group, method = service_acceptance.method.split(b'/')[1:3]
    except ValueError:
      logging.info('Illegal path "%s"!', service_acceptance.method)
      return
    request_deserializer = self._request_deserializers.get(
        (group, method), _IDENTITY)
    response_serializer = self._response_serializers.get(
        (group, method), _IDENTITY)

    call.read(call)
    context = _Context(call)
    self._rpc_states[call] = _RPCState(
        request_deserializer, response_serializer, 1, _Read.READING, None, 1,
        _HighWrite.OPEN, _LowWrite.OPEN, False, None, None, None,
        set((_READ, _FINISH,)), context)
    protocol = links.Protocol(links.Protocol.Kind.SERVICER_CONTEXT, context)
    ticket = links.Ticket(
        call, 0, group, method, links.Ticket.Subscription.FULL,
        service_acceptance.deadline - time.time(), None, event.metadata, None,
        None, None, None, None, protocol)
    self._relay.add_value(ticket)

  def _on_read_event(self, event):
    call = event.tag
    rpc_state = self._rpc_states[call]

    if event.bytes is None:
      rpc_state.read = _Read.CLOSED
      payload = None
      termination = links.Ticket.Termination.COMPLETION
      _no_longer_due(_READ, rpc_state, call, self._rpc_states)
    else:
      if 0 < rpc_state.allowance:
        payload = rpc_state.request_deserializer(event.bytes)
        termination = None
        rpc_state.allowance -= 1
        call.read(call)
      else:
        rpc_state.early_read = event.bytes
        _no_longer_due(_READ, rpc_state, call, self._rpc_states)
        return
        # TODO(issue 2916): Instead of returning:
        # rpc_state.read = _Read.AWAITING_ALLOWANCE
    ticket = links.Ticket(
        call, rpc_state.sequence_number, None, None, None, None, None, None,
        payload, None, None, None, termination, None)
    rpc_state.sequence_number += 1
    self._relay.add_value(ticket)

  def _on_write_event(self, event):
    call = event.tag
    rpc_state = self._rpc_states[call]

    if rpc_state.high_write is _HighWrite.CLOSED:
      if rpc_state.terminal_metadata is not None:
        _metadatafy(call, rpc_state.terminal_metadata)
      status = _status(
          links.Ticket.Termination.COMPLETION, rpc_state.code,
          rpc_state.message)
      call.status(status, call)
      rpc_state.low_write = _LowWrite.CLOSED
      rpc_state.due.add(_COMPLETE)
      rpc_state.due.remove(_WRITE)
    else:
      ticket = links.Ticket(
          call, rpc_state.sequence_number, None, None, None, None, 1, None,
          None, None, None, None, None, None)
      rpc_state.sequence_number += 1
      self._relay.add_value(ticket)
      rpc_state.low_write = _LowWrite.OPEN
      _no_longer_due(_WRITE, rpc_state, call, self._rpc_states)

  def _on_finish_event(self, event):
    call = event.tag
    rpc_state = self._rpc_states[call]
    _no_longer_due(_FINISH, rpc_state, call, self._rpc_states)
    code = event.status.code
    if code == _intermediary_low.Code.OK:
      return

    if code == _intermediary_low.Code.CANCELLED:
      termination = links.Ticket.Termination.CANCELLATION
    elif code == _intermediary_low.Code.DEADLINE_EXCEEDED:
      termination = links.Ticket.Termination.EXPIRATION
    else:
      termination = links.Ticket.Termination.TRANSMISSION_FAILURE
    ticket = links.Ticket(
        call, rpc_state.sequence_number, None, None, None, None, None, None,
        None, None, None, None, termination, None)
    rpc_state.sequence_number += 1
    self._relay.add_value(ticket)

  def _spin(self, completion_queue, server):
    while True:
      event = completion_queue.get(None)
      with self._lock:
        if event.kind is _STOP:
          self._due.remove(_STOP)
        elif event.kind is _READ:
          self._on_read_event(event)
        elif event.kind is _WRITE:
          self._on_write_event(event)
        elif event.kind is _COMPLETE:
          _no_longer_due(
              _COMPLETE, self._rpc_states.get(event.tag), event.tag,
              self._rpc_states)
        elif event.kind is _intermediary_low.Event.Kind.FINISH:
          self._on_finish_event(event)
        elif event.kind is _SERVICE:
          if self._server is None:
            self._due.remove(_SERVICE)
          else:
            self._on_service_acceptance_event(event, server)
        else:
          logging.error('Illegal event! %s', (event,))

        if not self._due and not self._rpc_states:
          completion_queue.stop()
          return

  def add_ticket(self, ticket):
    with self._lock:
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
            ticket.termination is not None or
            ticket.code is not None or
            ticket.message is not None):
          call.premetadata()
          rpc_state.premetadataed = True

      if ticket.allowance is not None:
        if rpc_state.early_read is None:
          rpc_state.allowance += ticket.allowance
        else:
          payload = rpc_state.request_deserializer(rpc_state.early_read)
          rpc_state.allowance += ticket.allowance - 1
          rpc_state.early_read = None
          if rpc_state.read is _Read.READING:
            call.read(call)
            rpc_state.due.add(_READ)
            termination = None
          else:
            termination = links.Ticket.Termination.COMPLETION
          early_read_ticket = links.Ticket(
              call, rpc_state.sequence_number, None, None, None, None, None,
              None, payload, None, None, None, termination, None)
          rpc_state.sequence_number += 1
          self._relay.add_value(early_read_ticket)

      if ticket.payload is not None:
        disable_compression = rpc_state.context.next_compression_disabled()
        if disable_compression:
          flags = _intermediary_low.WriteFlags.WRITE_NO_COMPRESS
        else:
          flags = 0
        call.write(rpc_state.response_serializer(ticket.payload), call, flags)
        rpc_state.due.add(_WRITE)
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
          status = _status(
              links.Ticket.Termination.COMPLETION, rpc_state.code,
              rpc_state.message)
          call.status(status, call)
          rpc_state.due.add(_COMPLETE)
          rpc_state.low_write = _LowWrite.CLOSED
      elif ticket.termination is not None:
        if rpc_state.terminal_metadata is not None:
          _metadatafy(call, rpc_state.terminal_metadata)
        status = _status(
            ticket.termination, rpc_state.code, rpc_state.message)
        call.status(status, call)
        rpc_state.due.add(_COMPLETE)

  def add_port(self, address, server_credentials):
    with self._lock:
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
      self._due.add(_SERVICE)

  def begin_stop(self):
    with self._lock:
      self._server.stop()
      self._due.add(_STOP)
      self._server = None

  def end_stop(self):
    with self._lock:
      pool = self._pool
    pool.shutdown(wait=True)


class ServiceLink(links.Link):
  """A links.Link for use on the service-side of a gRPC connection.

  Implementations of this interface are only valid for use between calls to
  their start method and one of their stop methods.
  """

  @abc.abstractmethod
  def add_port(self, address, server_credentials):
    """Adds a port on which to service RPCs after this link has been started.

    Args:
      address: The address on which to service RPCs with a port number of zero
        requesting that a port number be automatically selected and used.
      server_credentials: An _intermediary_low.ServerCredentials object, or
        None for insecure service.

    Returns:
      An integer port on which RPCs will be serviced after this link has been
        started. This is typically the same number as the port number contained
        in the passed address, but will likely be different if the port number
        contained in the passed address was zero.
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
  def begin_stop(self):
    """Indicate imminent link stop and immediate rejection of new RPCs.

    New RPCs will be rejected as soon as this method is called, but ongoing RPCs
    will be allowed to continue until they terminate. This method does not
    block.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def end_stop(self):
    """Finishes stopping this link.

    begin_stop must have been called exactly once before calling this method.

    All in-progress RPCs will be terminated immediately.
    """
    raise NotImplementedError()


class _ServiceLink(ServiceLink):

  def __init__(self, request_deserializers, response_serializers):
    self._relay = relay.relay(None)
    self._kernel = _Kernel(
        {} if request_deserializers is None else request_deserializers,
        {} if response_serializers is None else response_serializers,
        self._relay)

  def accept_ticket(self, ticket):
    self._kernel.add_ticket(ticket)

  def join_link(self, link):
    self._relay.set_behavior(link.accept_ticket)

  def add_port(self, address, server_credentials):
    return self._kernel.add_port(address, server_credentials)

  def start(self):
    self._relay.start()
    return self._kernel.start()

  def begin_stop(self):
    self._kernel.begin_stop()

  def end_stop(self):
    self._kernel.end_stop()
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
