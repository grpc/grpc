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

"""State and behavior for packet reception."""

import abc

from grpc.framework.base import interfaces
from grpc.framework.base import _interfaces

_INITIAL_FRONT_TO_BACK_PACKET_KINDS = (
    interfaces.FrontToBackPacket.Kind.COMMENCEMENT,
    interfaces.FrontToBackPacket.Kind.ENTIRE,
)


class _Receiver(object):
  """Common specification of different packet-handling behavior."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def abort_if_abortive(self, packet):
    """Aborts the operation if the packet is abortive.

    Args:
      packet: A just-arrived packet.

    Returns:
      A boolean indicating whether or not this Receiver aborted the operation
        based on the packet.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def receive(self, packet):
    """Handles a just-arrived packet.

    Args:
      packet: A just-arrived packet.

    Returns:
      A boolean indicating whether or not the packet was terminal (i.e. whether
        or not non-abortive packets are legal after this one).
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def reception_failure(self):
    """Aborts the operation with an indication of reception failure."""
    raise NotImplementedError()


def _abort(
    outcome, termination_manager, transmission_manager, ingestion_manager,
    expiration_manager):
  """Indicates abortion with the given outcome to the given managers."""
  termination_manager.abort(outcome)
  transmission_manager.abort(outcome)
  ingestion_manager.abort()
  expiration_manager.abort()


def _abort_if_abortive(
    packet, abortive, termination_manager, transmission_manager,
    ingestion_manager, expiration_manager):
  """Determines a packet's being abortive and if so aborts the operation.

  Args:
    packet: A just-arrived packet.
    abortive: A callable that takes a packet and returns an interfaces.Outcome
      indicating that the operation should be aborted or None indicating that
      the operation should not be aborted.
    termination_manager: The operation's _interfaces.TerminationManager.
    transmission_manager: The operation's _interfaces.TransmissionManager.
    ingestion_manager: The operation's _interfaces.IngestionManager.
    expiration_manager: The operation's _interfaces.ExpirationManager.

  Returns:
    True if the operation was aborted; False otherwise.
  """
  abortion_outcome = abortive(packet)
  if abortion_outcome is None:
    return False
  else:
    _abort(
        abortion_outcome, termination_manager, transmission_manager,
        ingestion_manager, expiration_manager)
    return True


def _reception_failure(
    termination_manager, transmission_manager, ingestion_manager,
    expiration_manager):
  """Aborts the operation with an indication of reception failure."""
  _abort(
      interfaces.Outcome.RECEPTION_FAILURE, termination_manager,
      transmission_manager, ingestion_manager, expiration_manager)


class _BackReceiver(_Receiver):
  """Packet-handling specific to the back side of an operation."""

  def __init__(
      self, termination_manager, transmission_manager, ingestion_manager,
      expiration_manager):
    """Constructor.

    Args:
      termination_manager: The operation's _interfaces.TerminationManager.
      transmission_manager: The operation's _interfaces.TransmissionManager.
      ingestion_manager: The operation's _interfaces.IngestionManager.
      expiration_manager: The operation's _interfaces.ExpirationManager.
    """
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._ingestion_manager = ingestion_manager
    self._expiration_manager = expiration_manager

    self._first_packet_seen = False
    self._last_packet_seen = False

  def _abortive(self, packet):
    """Determines whether or not (and if so, how) a packet is abortive.

    Args:
      packet: A just-arrived packet.

    Returns:
      An interfaces.Outcome value describing operation abortion if the
        packet is abortive or None if the packet is not abortive.
    """
    if packet.kind is interfaces.FrontToBackPacket.Kind.CANCELLATION:
      return interfaces.Outcome.CANCELLED
    elif packet.kind is interfaces.FrontToBackPacket.Kind.EXPIRATION:
      return interfaces.Outcome.EXPIRED
    elif packet.kind is interfaces.FrontToBackPacket.Kind.SERVICED_FAILURE:
      return interfaces.Outcome.SERVICED_FAILURE
    elif packet.kind is interfaces.FrontToBackPacket.Kind.RECEPTION_FAILURE:
      return interfaces.Outcome.SERVICED_FAILURE
    elif (packet.kind in _INITIAL_FRONT_TO_BACK_PACKET_KINDS and
          self._first_packet_seen):
      return interfaces.Outcome.RECEPTION_FAILURE
    elif self._last_packet_seen:
      return interfaces.Outcome.RECEPTION_FAILURE
    else:
      return None

  def abort_if_abortive(self, packet):
    """See _Receiver.abort_if_abortive for specification."""
    return _abort_if_abortive(
        packet, self._abortive, self._termination_manager,
        self._transmission_manager, self._ingestion_manager,
        self._expiration_manager)

  def receive(self, packet):
    """See _Receiver.receive for specification."""
    if packet.timeout is not None:
      self._expiration_manager.change_timeout(packet.timeout)

    if packet.kind is interfaces.FrontToBackPacket.Kind.COMMENCEMENT:
      self._first_packet_seen = True
      self._ingestion_manager.start(packet.name)
      if packet.payload is not None:
        self._ingestion_manager.consume(packet.payload)
    elif packet.kind is interfaces.FrontToBackPacket.Kind.CONTINUATION:
      self._ingestion_manager.consume(packet.payload)
    elif packet.kind is interfaces.FrontToBackPacket.Kind.COMPLETION:
      self._last_packet_seen = True
      if packet.payload is None:
        self._ingestion_manager.terminate()
      else:
        self._ingestion_manager.consume_and_terminate(packet.payload)
    else:
      self._first_packet_seen = True
      self._last_packet_seen = True
      self._ingestion_manager.start(packet.name)
      if packet.payload is None:
        self._ingestion_manager.terminate()
      else:
        self._ingestion_manager.consume_and_terminate(packet.payload)

  def reception_failure(self):
    """See _Receiver.reception_failure for specification."""
    _reception_failure(
        self._termination_manager, self._transmission_manager,
        self._ingestion_manager, self._expiration_manager)


class _FrontReceiver(_Receiver):
  """Packet-handling specific to the front side of an operation."""

  def __init__(
      self, termination_manager, transmission_manager, ingestion_manager,
      expiration_manager):
    """Constructor.

    Args:
      termination_manager: The operation's _interfaces.TerminationManager.
      transmission_manager: The operation's _interfaces.TransmissionManager.
      ingestion_manager: The operation's _interfaces.IngestionManager.
      expiration_manager: The operation's _interfaces.ExpirationManager.
    """
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._ingestion_manager = ingestion_manager
    self._expiration_manager = expiration_manager

    self._last_packet_seen = False

  def _abortive(self, packet):
    """Determines whether or not (and if so, how) a packet is abortive.

    Args:
      packet: A just-arrived packet.

    Returns:
      An interfaces.Outcome value describing operation abortion if the packet
        is abortive or None if the packet is not abortive.
    """
    if packet.kind is interfaces.BackToFrontPacket.Kind.CANCELLATION:
      return interfaces.Outcome.CANCELLED
    elif packet.kind is interfaces.BackToFrontPacket.Kind.EXPIRATION:
      return interfaces.Outcome.EXPIRED
    elif packet.kind is interfaces.BackToFrontPacket.Kind.SERVICER_FAILURE:
      return interfaces.Outcome.SERVICER_FAILURE
    elif packet.kind is interfaces.BackToFrontPacket.Kind.RECEPTION_FAILURE:
      return interfaces.Outcome.SERVICER_FAILURE
    elif self._last_packet_seen:
      return interfaces.Outcome.RECEPTION_FAILURE
    else:
      return None

  def abort_if_abortive(self, packet):
    """See _Receiver.abort_if_abortive for specification."""
    return _abort_if_abortive(
        packet, self._abortive, self._termination_manager,
        self._transmission_manager, self._ingestion_manager,
        self._expiration_manager)

  def receive(self, packet):
    """See _Receiver.receive for specification."""
    if packet.kind is interfaces.BackToFrontPacket.Kind.CONTINUATION:
      self._ingestion_manager.consume(packet.payload)
    elif packet.kind is interfaces.BackToFrontPacket.Kind.COMPLETION:
      self._last_packet_seen = True
      if packet.payload is None:
        self._ingestion_manager.terminate()
      else:
        self._ingestion_manager.consume_and_terminate(packet.payload)

  def reception_failure(self):
    """See _Receiver.reception_failure for specification."""
    _reception_failure(
        self._termination_manager, self._transmission_manager,
        self._ingestion_manager, self._expiration_manager)


class _ReceptionManager(_interfaces.ReceptionManager):
  """A ReceptionManager based around a _Receiver passed to it."""

  def __init__(self, lock, receiver):
    """Constructor.

    Args:
      lock: The operation-servicing-wide lock object.
      receiver: A _Receiver responsible for handling received packets.
    """
    self._lock = lock
    self._receiver = receiver

    self._lowest_unseen_sequence_number = 0
    self._out_of_sequence_packets = {}
    self._completed_sequence_number = None
    self._aborted = False

  def _sequence_failure(self, packet):
    """Determines a just-arrived packet's sequential legitimacy.

    Args:
      packet: A just-arrived packet.

    Returns:
      True if the packet is sequentially legitimate; False otherwise.
    """
    if packet.sequence_number < self._lowest_unseen_sequence_number:
      return True
    elif packet.sequence_number in self._out_of_sequence_packets:
      return True
    elif (self._completed_sequence_number is not None and
          self._completed_sequence_number <= packet.sequence_number):
      return True
    else:
      return False

  def _process(self, packet):
    """Process those packets ready to be processed.

    Args:
      packet: A just-arrived packet the sequence number of which matches this
        _ReceptionManager's _lowest_unseen_sequence_number field.
    """
    while True:
      completed = self._receiver.receive(packet)
      if completed:
        self._out_of_sequence_packets.clear()
        self._completed_sequence_number = packet.sequence_number
        self._lowest_unseen_sequence_number = packet.sequence_number + 1
        return
      else:
        next_packet = self._out_of_sequence_packets.pop(
            packet.sequence_number + 1, None)
        if next_packet is None:
          self._lowest_unseen_sequence_number = packet.sequence_number + 1
          return
        else:
          packet = next_packet

  def receive_packet(self, packet):
    """See _interfaces.ReceptionManager.receive_packet for specification."""
    with self._lock:
      if self._aborted:
        return
      elif self._sequence_failure(packet):
        self._receiver.reception_failure()
        self._aborted = True
      elif self._receiver.abort_if_abortive(packet):
        self._aborted = True
      elif packet.sequence_number == self._lowest_unseen_sequence_number:
        self._process(packet)
      else:
        self._out_of_sequence_packets[packet.sequence_number] = packet


def front_reception_manager(
    lock, termination_manager, transmission_manager, ingestion_manager,
    expiration_manager):
  """Creates a _interfaces.ReceptionManager for front-side use.

  Args:
    lock: The operation-servicing-wide lock object.
    termination_manager: The operation's _interfaces.TerminationManager.
    transmission_manager: The operation's _interfaces.TransmissionManager.
    ingestion_manager: The operation's _interfaces.IngestionManager.
    expiration_manager: The operation's _interfaces.ExpirationManager.

  Returns:
    A _interfaces.ReceptionManager appropriate for front-side use.
  """
  return _ReceptionManager(
      lock, _FrontReceiver(
          termination_manager, transmission_manager, ingestion_manager,
          expiration_manager))


def back_reception_manager(
    lock, termination_manager, transmission_manager, ingestion_manager,
    expiration_manager):
  """Creates a _interfaces.ReceptionManager for back-side use.

  Args:
    lock: The operation-servicing-wide lock object.
    termination_manager: The operation's _interfaces.TerminationManager.
    transmission_manager: The operation's _interfaces.TransmissionManager.
    ingestion_manager: The operation's _interfaces.IngestionManager.
    expiration_manager: The operation's _interfaces.ExpirationManager.

  Returns:
    A _interfaces.ReceptionManager appropriate for back-side use.
  """
  return _ReceptionManager(
      lock, _BackReceiver(
          termination_manager, transmission_manager, ingestion_manager,
          expiration_manager))
