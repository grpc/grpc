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

"""State and behavior for ticket reception."""

import abc

from grpc.framework.base import interfaces
from grpc.framework.base import _interfaces

_INITIAL_FRONT_TO_BACK_TICKET_KINDS = (
    interfaces.FrontToBackTicket.Kind.COMMENCEMENT,
    interfaces.FrontToBackTicket.Kind.ENTIRE,
)


class _Receiver(object):
  """Common specification of different ticket-handling behavior."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def abort_if_abortive(self, ticket):
    """Aborts the operation if the ticket is abortive.

    Args:
      ticket: A just-arrived ticket.

    Returns:
      A boolean indicating whether or not this Receiver aborted the operation
        based on the ticket.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def receive(self, ticket):
    """Handles a just-arrived ticket.

    Args:
      ticket: A just-arrived ticket.

    Returns:
      A boolean indicating whether or not the ticket was terminal (i.e. whether
        or not non-abortive tickets are legal after this one).
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
    ticket, abortive, termination_manager, transmission_manager,
    ingestion_manager, expiration_manager):
  """Determines a ticket's being abortive and if so aborts the operation.

  Args:
    ticket: A just-arrived ticket.
    abortive: A callable that takes a ticket and returns an interfaces.Outcome
      indicating that the operation should be aborted or None indicating that
      the operation should not be aborted.
    termination_manager: The operation's _interfaces.TerminationManager.
    transmission_manager: The operation's _interfaces.TransmissionManager.
    ingestion_manager: The operation's _interfaces.IngestionManager.
    expiration_manager: The operation's _interfaces.ExpirationManager.

  Returns:
    True if the operation was aborted; False otherwise.
  """
  abortion_outcome = abortive(ticket)
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
  """Ticket-handling specific to the back side of an operation."""

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

    self._first_ticket_seen = False
    self._last_ticket_seen = False

  def _abortive(self, ticket):
    """Determines whether or not (and if so, how) a ticket is abortive.

    Args:
      ticket: A just-arrived ticket.

    Returns:
      An interfaces.Outcome value describing operation abortion if the
        ticket is abortive or None if the ticket is not abortive.
    """
    if ticket.kind is interfaces.FrontToBackTicket.Kind.CANCELLATION:
      return interfaces.Outcome.CANCELLED
    elif ticket.kind is interfaces.FrontToBackTicket.Kind.EXPIRATION:
      return interfaces.Outcome.EXPIRED
    elif ticket.kind is interfaces.FrontToBackTicket.Kind.SERVICED_FAILURE:
      return interfaces.Outcome.SERVICED_FAILURE
    elif ticket.kind is interfaces.FrontToBackTicket.Kind.RECEPTION_FAILURE:
      return interfaces.Outcome.SERVICED_FAILURE
    elif (ticket.kind in _INITIAL_FRONT_TO_BACK_TICKET_KINDS and
          self._first_ticket_seen):
      return interfaces.Outcome.RECEPTION_FAILURE
    elif self._last_ticket_seen:
      return interfaces.Outcome.RECEPTION_FAILURE
    else:
      return None

  def abort_if_abortive(self, ticket):
    """See _Receiver.abort_if_abortive for specification."""
    return _abort_if_abortive(
        ticket, self._abortive, self._termination_manager,
        self._transmission_manager, self._ingestion_manager,
        self._expiration_manager)

  def receive(self, ticket):
    """See _Receiver.receive for specification."""
    if ticket.timeout is not None:
      self._expiration_manager.change_timeout(ticket.timeout)

    if ticket.kind is interfaces.FrontToBackTicket.Kind.COMMENCEMENT:
      self._first_ticket_seen = True
      self._ingestion_manager.start(ticket.name)
      if ticket.payload is not None:
        self._ingestion_manager.consume(ticket.payload)
    elif ticket.kind is interfaces.FrontToBackTicket.Kind.CONTINUATION:
      self._ingestion_manager.consume(ticket.payload)
    elif ticket.kind is interfaces.FrontToBackTicket.Kind.COMPLETION:
      self._last_ticket_seen = True
      if ticket.payload is None:
        self._ingestion_manager.terminate()
      else:
        self._ingestion_manager.consume_and_terminate(ticket.payload)
    else:
      self._first_ticket_seen = True
      self._last_ticket_seen = True
      self._ingestion_manager.start(ticket.name)
      if ticket.payload is None:
        self._ingestion_manager.terminate()
      else:
        self._ingestion_manager.consume_and_terminate(ticket.payload)

  def reception_failure(self):
    """See _Receiver.reception_failure for specification."""
    _reception_failure(
        self._termination_manager, self._transmission_manager,
        self._ingestion_manager, self._expiration_manager)


class _FrontReceiver(_Receiver):
  """Ticket-handling specific to the front side of an operation."""

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

    self._last_ticket_seen = False

  def _abortive(self, ticket):
    """Determines whether or not (and if so, how) a ticket is abortive.

    Args:
      ticket: A just-arrived ticket.

    Returns:
      An interfaces.Outcome value describing operation abortion if the ticket
        is abortive or None if the ticket is not abortive.
    """
    if ticket.kind is interfaces.BackToFrontTicket.Kind.CANCELLATION:
      return interfaces.Outcome.CANCELLED
    elif ticket.kind is interfaces.BackToFrontTicket.Kind.EXPIRATION:
      return interfaces.Outcome.EXPIRED
    elif ticket.kind is interfaces.BackToFrontTicket.Kind.SERVICER_FAILURE:
      return interfaces.Outcome.SERVICER_FAILURE
    elif ticket.kind is interfaces.BackToFrontTicket.Kind.RECEPTION_FAILURE:
      return interfaces.Outcome.SERVICER_FAILURE
    elif self._last_ticket_seen:
      return interfaces.Outcome.RECEPTION_FAILURE
    else:
      return None

  def abort_if_abortive(self, ticket):
    """See _Receiver.abort_if_abortive for specification."""
    return _abort_if_abortive(
        ticket, self._abortive, self._termination_manager,
        self._transmission_manager, self._ingestion_manager,
        self._expiration_manager)

  def receive(self, ticket):
    """See _Receiver.receive for specification."""
    if ticket.kind is interfaces.BackToFrontTicket.Kind.CONTINUATION:
      self._ingestion_manager.consume(ticket.payload)
    elif ticket.kind is interfaces.BackToFrontTicket.Kind.COMPLETION:
      self._last_ticket_seen = True
      if ticket.payload is None:
        self._ingestion_manager.terminate()
      else:
        self._ingestion_manager.consume_and_terminate(ticket.payload)

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
      receiver: A _Receiver responsible for handling received tickets.
    """
    self._lock = lock
    self._receiver = receiver

    self._lowest_unseen_sequence_number = 0
    self._out_of_sequence_tickets = {}
    self._completed_sequence_number = None
    self._aborted = False

  def _sequence_failure(self, ticket):
    """Determines a just-arrived ticket's sequential legitimacy.

    Args:
      ticket: A just-arrived ticket.

    Returns:
      True if the ticket is sequentially legitimate; False otherwise.
    """
    if ticket.sequence_number < self._lowest_unseen_sequence_number:
      return True
    elif ticket.sequence_number in self._out_of_sequence_tickets:
      return True
    elif (self._completed_sequence_number is not None and
          self._completed_sequence_number <= ticket.sequence_number):
      return True
    else:
      return False

  def _process(self, ticket):
    """Process those tickets ready to be processed.

    Args:
      ticket: A just-arrived ticket the sequence number of which matches this
        _ReceptionManager's _lowest_unseen_sequence_number field.
    """
    while True:
      completed = self._receiver.receive(ticket)
      if completed:
        self._out_of_sequence_tickets.clear()
        self._completed_sequence_number = ticket.sequence_number
        self._lowest_unseen_sequence_number = ticket.sequence_number + 1
        return
      else:
        next_ticket = self._out_of_sequence_tickets.pop(
            ticket.sequence_number + 1, None)
        if next_ticket is None:
          self._lowest_unseen_sequence_number = ticket.sequence_number + 1
          return
        else:
          ticket = next_ticket

  def receive_ticket(self, ticket):
    """See _interfaces.ReceptionManager.receive_ticket for specification."""
    with self._lock:
      if self._aborted:
        return
      elif self._sequence_failure(ticket):
        self._receiver.reception_failure()
        self._aborted = True
      elif self._receiver.abort_if_abortive(ticket):
        self._aborted = True
      elif ticket.sequence_number == self._lowest_unseen_sequence_number:
        self._process(ticket)
      else:
        self._out_of_sequence_tickets[ticket.sequence_number] = ticket


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
