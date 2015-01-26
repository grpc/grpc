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

"""State and behavior for packet transmission during an operation."""

import abc

from _framework.base import interfaces
from _framework.base.packets import _constants
from _framework.base.packets import _interfaces
from _framework.base.packets import packets
from _framework.foundation import callable_util

_TRANSMISSION_EXCEPTION_LOG_MESSAGE = 'Exception during transmission!'

_FRONT_TO_BACK_NO_TRANSMISSION_KINDS = (
    packets.Kind.SERVICER_FAILURE,
    )
_BACK_TO_FRONT_NO_TRANSMISSION_KINDS = (
    packets.Kind.CANCELLATION,
    packets.Kind.SERVICED_FAILURE,
    )


class _Packetizer(object):
  """Common specification of different packet-creating behavior."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def packetize(self, operation_id, sequence_number, payload, complete):
    """Creates a packet indicating ordinary operation progress.

    Args:
      operation_id: The operation ID for the current operation.
      sequence_number: A sequence number for the packet.
      payload: A customer payload object. May be None if sequence_number is
        zero or complete is true.
      complete: A boolean indicating whether or not the packet should describe
        itself as (but for a later indication of operation abortion) the last
        packet to be sent.

    Returns:
      An object of an appropriate type suitable for transmission to the other
        side of the operation.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def packetize_abortion(self, operation_id, sequence_number, kind):
    """Creates a packet indicating that the operation is aborted.

    Args:
      operation_id: The operation ID for the current operation.
      sequence_number: A sequence number for the packet.
      kind: One of the values of packets.Kind indicating operational abortion.

    Returns:
      An object of an appropriate type suitable for transmission to the other
        side of the operation, or None if transmission is not appropriate for
        the given kind.
    """
    raise NotImplementedError()


class _FrontPacketizer(_Packetizer):
  """Front-side packet-creating behavior."""

  def __init__(self, name, subscription, trace_id, timeout):
    """Constructor.

    Args:
      name: The name of the operation.
      subscription: One of interfaces.FULL, interfaces.TERMINATION_ONLY, or
        interfaces.NONE describing the interest the front has in packets sent
        from the back.
      trace_id: A uuid.UUID identifying a set of related operations to which
        this operation belongs.
      timeout: A length of time in seconds to allow for the entire operation.
    """
    self._name = name
    self._subscription = subscription
    self._trace_id = trace_id
    self._timeout = timeout

  def packetize(self, operation_id, sequence_number, payload, complete):
    """See _Packetizer.packetize for specification."""
    if sequence_number:
      return packets.FrontToBackPacket(
          operation_id, sequence_number,
          packets.Kind.COMPLETION if complete else packets.Kind.CONTINUATION,
          self._name, self._subscription, self._trace_id, payload,
          self._timeout)
    else:
      return packets.FrontToBackPacket(
          operation_id, 0,
          packets.Kind.ENTIRE if complete else packets.Kind.COMMENCEMENT,
          self._name, self._subscription, self._trace_id, payload,
          self._timeout)

  def packetize_abortion(self, operation_id, sequence_number, kind):
    """See _Packetizer.packetize_abortion for specification."""
    if kind in _FRONT_TO_BACK_NO_TRANSMISSION_KINDS:
      return None
    else:
      return packets.FrontToBackPacket(
          operation_id, sequence_number, kind, None, None, None, None, None)


class _BackPacketizer(_Packetizer):
  """Back-side packet-creating behavior."""

  def packetize(self, operation_id, sequence_number, payload, complete):
    """See _Packetizer.packetize for specification."""
    return packets.BackToFrontPacket(
        operation_id, sequence_number,
        packets.Kind.COMPLETION if complete else packets.Kind.CONTINUATION,
        payload)

  def packetize_abortion(self, operation_id, sequence_number, kind):
    """See _Packetizer.packetize_abortion for specification."""
    if kind in _BACK_TO_FRONT_NO_TRANSMISSION_KINDS:
      return None
    else:
      return packets.BackToFrontPacket(
          operation_id, sequence_number, kind, None)


class TransmissionManager(_interfaces.TransmissionManager):
  """A _interfaces.TransmissionManager on which other managers may be set."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def set_ingestion_and_expiration_managers(
      self, ingestion_manager, expiration_manager):
    """Sets two of the other managers with which this manager may interact.

    Args:
      ingestion_manager: The _interfaces.IngestionManager associated with the
        current operation.
      expiration_manager: The _interfaces.ExpirationManager associated with the
        current operation.
    """
    raise NotImplementedError()


class _EmptyTransmissionManager(TransmissionManager):
  """A completely no-operative _interfaces.TransmissionManager."""

  def set_ingestion_and_expiration_managers(
      self, ingestion_manager, expiration_manager):
    """See overriden method for specification."""

  def inmit(self, emission, complete):
    """See _interfaces.TransmissionManager.inmit for specification."""

  def abort(self, category):
    """See _interfaces.TransmissionManager.abort for specification."""


class _TransmittingTransmissionManager(TransmissionManager):
  """A TransmissionManager implementation that sends packets."""

  def __init__(
      self, lock, pool, callback, operation_id, packetizer,
      termination_manager):
    """Constructor.

    Args:
      lock: The operation-servicing-wide lock object.
      pool: A thread pool in which the work of transmitting packets will be
        performed.
      callback: A callable that accepts packets and sends them to the other side
        of the operation.
      operation_id: The operation's ID.
      packetizer: A _Packetizer for packet creation.
      termination_manager: The _interfaces.TerminationManager associated with
        this operation.
    """
    self._lock = lock
    self._pool = pool
    self._callback = callback
    self._operation_id = operation_id
    self._packetizer = packetizer
    self._termination_manager = termination_manager
    self._ingestion_manager = None
    self._expiration_manager = None

    self._emissions = []
    self._emission_complete = False
    self._kind = None
    self._lowest_unused_sequence_number = 0
    self._transmitting = False

  def set_ingestion_and_expiration_managers(
      self, ingestion_manager, expiration_manager):
    """See overridden method for specification."""
    self._ingestion_manager = ingestion_manager
    self._expiration_manager = expiration_manager

  def _lead_packet(self, emission, complete):
    """Creates a packet suitable for leading off the transmission loop.

    Args:
      emission: A customer payload object to be sent to the other side of the
        operation.
      complete: Whether or not the sequence of customer payloads ends with
        the passed object.

    Returns:
      A packet with which to lead off the transmission loop.
    """
    sequence_number = self._lowest_unused_sequence_number
    self._lowest_unused_sequence_number += 1
    return self._packetizer.packetize(
        self._operation_id, sequence_number, emission, complete)

  def _abortive_response_packet(self, kind):
    """Creates a packet indicating operation abortion.

    Args:
      kind: One of the values of packets.Kind indicating operational abortion.

    Returns:
      A packet indicating operation abortion.
    """
    packet = self._packetizer.packetize_abortion(
        self._operation_id, self._lowest_unused_sequence_number, kind)
    if packet is None:
      return None
    else:
      self._lowest_unused_sequence_number += 1
      return packet

  def _next_packet(self):
    """Creates the next packet to be sent to the other side of the operation.

    Returns:
      A (completed, packet) tuple comprised of a boolean indicating whether or
        not the sequence of packets has completed normally and a packet to send
        to the other side if the sequence of packets hasn't completed. The tuple
        will never have both a True first element and a non-None second element.
    """
    if self._emissions is None:
      return False, None
    elif self._kind is None:
      if self._emissions:
        payload = self._emissions.pop(0)
        complete = self._emission_complete and not self._emissions
        sequence_number = self._lowest_unused_sequence_number
        self._lowest_unused_sequence_number += 1
        return complete, self._packetizer.packetize(
            self._operation_id, sequence_number, payload, complete)
      else:
        return self._emission_complete, None
    else:
      packet = self._abortive_response_packet(self._kind)
      self._emissions = None
      return False, None if packet is None else packet

  def _transmit(self, packet):
    """Commences the transmission loop sending packets.

    Args:
      packet: A packet to be sent to the other side of the operation.
    """
    def transmit(packet):
      while True:
        transmission_outcome = callable_util.call_logging_exceptions(
            self._callback, _TRANSMISSION_EXCEPTION_LOG_MESSAGE, packet)
        if transmission_outcome.exception is None:
          with self._lock:
            complete, packet = self._next_packet()
            if packet is None:
              if complete:
                self._termination_manager.transmission_complete()
              self._transmitting = False
              return
        else:
          with self._lock:
            self._emissions = None
            self._termination_manager.abort(packets.Kind.TRANSMISSION_FAILURE)
            self._ingestion_manager.abort()
            self._expiration_manager.abort()
            self._transmitting = False
            return

    self._pool.submit(callable_util.with_exceptions_logged(
        transmit, _constants.INTERNAL_ERROR_LOG_MESSAGE), packet)
    self._transmitting = True

  def inmit(self, emission, complete):
    """See _interfaces.TransmissionManager.inmit for specification."""
    if self._emissions is not None and self._kind is None:
      self._emission_complete = complete
      if self._transmitting:
        self._emissions.append(emission)
      else:
        self._transmit(self._lead_packet(emission, complete))

  def abort(self, kind):
    """See _interfaces.TransmissionManager.abort for specification."""
    if self._emissions is not None and self._kind is None:
      self._kind = kind
      if not self._transmitting:
        packet = self._abortive_response_packet(kind)
        self._emissions = None
        if packet is not None:
          self._transmit(packet)


def front_transmission_manager(
    lock, pool, callback, operation_id, name, subscription, trace_id, timeout,
    termination_manager):
  """Creates a TransmissionManager appropriate for front-side use.

  Args:
    lock: The operation-servicing-wide lock object.
    pool: A thread pool in which the work of transmitting packets will be
      performed.
    callback: A callable that accepts packets and sends them to the other side
      of the operation.
    operation_id: The operation's ID.
    name: The name of the operation.
    subscription: One of interfaces.FULL, interfaces.TERMINATION_ONLY, or
      interfaces.NONE describing the interest the front has in packets sent
      from the back.
    trace_id: A uuid.UUID identifying a set of related operations to which
      this operation belongs.
    timeout: A length of time in seconds to allow for the entire operation.
    termination_manager: The _interfaces.TerminationManager associated with
      this operation.

  Returns:
    A TransmissionManager appropriate for front-side use.
  """
  return _TransmittingTransmissionManager(
      lock, pool, callback, operation_id, _FrontPacketizer(
          name, subscription, trace_id, timeout),
      termination_manager)


def back_transmission_manager(
    lock, pool, callback, operation_id, termination_manager, subscription):
  """Creates a TransmissionManager appropriate for back-side use.

  Args:
    lock: The operation-servicing-wide lock object.
    pool: A thread pool in which the work of transmitting packets will be
      performed.
    callback: A callable that accepts packets and sends them to the other side
      of the operation.
    operation_id: The operation's ID.
    termination_manager: The _interfaces.TerminationManager associated with
      this operation.
    subscription: One of interfaces.FULL, interfaces.TERMINATION_ONLY, or
      interfaces.NONE describing the interest the front has in packets sent from
      the back.

  Returns:
    A TransmissionManager appropriate for back-side use.
  """
  if subscription == interfaces.NONE:
    return _EmptyTransmissionManager()
  else:
    return _TransmittingTransmissionManager(
        lock, pool, callback, operation_id, _BackPacketizer(),
        termination_manager)
