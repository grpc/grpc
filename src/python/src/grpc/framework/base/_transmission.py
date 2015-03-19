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

"""State and behavior for ticket transmission during an operation."""

import abc

from grpc.framework.base import _constants
from grpc.framework.base import _interfaces
from grpc.framework.base import interfaces
from grpc.framework.foundation import callable_util

_TRANSMISSION_EXCEPTION_LOG_MESSAGE = 'Exception during transmission!'

_FRONT_TO_BACK_NO_TRANSMISSION_OUTCOMES = (
    interfaces.Outcome.SERVICER_FAILURE,
    )
_BACK_TO_FRONT_NO_TRANSMISSION_OUTCOMES = (
    interfaces.Outcome.CANCELLED,
    interfaces.Outcome.SERVICED_FAILURE,
    )

_ABORTION_OUTCOME_TO_FRONT_TO_BACK_TICKET_KIND = {
    interfaces.Outcome.CANCELLED:
        interfaces.FrontToBackTicket.Kind.CANCELLATION,
    interfaces.Outcome.EXPIRED:
        interfaces.FrontToBackTicket.Kind.EXPIRATION,
    interfaces.Outcome.RECEPTION_FAILURE:
        interfaces.FrontToBackTicket.Kind.RECEPTION_FAILURE,
    interfaces.Outcome.TRANSMISSION_FAILURE:
        interfaces.FrontToBackTicket.Kind.TRANSMISSION_FAILURE,
    interfaces.Outcome.SERVICED_FAILURE:
        interfaces.FrontToBackTicket.Kind.SERVICED_FAILURE,
    interfaces.Outcome.SERVICER_FAILURE:
        interfaces.FrontToBackTicket.Kind.SERVICER_FAILURE,
}

_ABORTION_OUTCOME_TO_BACK_TO_FRONT_TICKET_KIND = {
    interfaces.Outcome.CANCELLED:
        interfaces.BackToFrontTicket.Kind.CANCELLATION,
    interfaces.Outcome.EXPIRED:
        interfaces.BackToFrontTicket.Kind.EXPIRATION,
    interfaces.Outcome.RECEPTION_FAILURE:
        interfaces.BackToFrontTicket.Kind.RECEPTION_FAILURE,
    interfaces.Outcome.TRANSMISSION_FAILURE:
        interfaces.BackToFrontTicket.Kind.TRANSMISSION_FAILURE,
    interfaces.Outcome.SERVICED_FAILURE:
        interfaces.BackToFrontTicket.Kind.SERVICED_FAILURE,
    interfaces.Outcome.SERVICER_FAILURE:
        interfaces.BackToFrontTicket.Kind.SERVICER_FAILURE,
}


class _Ticketizer(object):
  """Common specification of different ticket-creating behavior."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def ticketize(self, operation_id, sequence_number, payload, complete):
    """Creates a ticket indicating ordinary operation progress.

    Args:
      operation_id: The operation ID for the current operation.
      sequence_number: A sequence number for the ticket.
      payload: A customer payload object. May be None if sequence_number is
        zero or complete is true.
      complete: A boolean indicating whether or not the ticket should describe
        itself as (but for a later indication of operation abortion) the last
        ticket to be sent.

    Returns:
      An object of an appropriate type suitable for transmission to the other
        side of the operation.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def ticketize_abortion(self, operation_id, sequence_number, outcome):
    """Creates a ticket indicating that the operation is aborted.

    Args:
      operation_id: The operation ID for the current operation.
      sequence_number: A sequence number for the ticket.
      outcome: An interfaces.Outcome value describing the operation abortion.

    Returns:
      An object of an appropriate type suitable for transmission to the other
        side of the operation, or None if transmission is not appropriate for
        the given outcome.
    """
    raise NotImplementedError()


class _FrontTicketizer(_Ticketizer):
  """Front-side ticket-creating behavior."""

  def __init__(self, name, subscription_kind, trace_id, timeout):
    """Constructor.

    Args:
      name: The name of the operation.
      subscription_kind: An interfaces.ServicedSubscription.Kind value
        describing the interest the front has in tickets sent from the back.
      trace_id: A uuid.UUID identifying a set of related operations to which
        this operation belongs.
      timeout: A length of time in seconds to allow for the entire operation.
    """
    self._name = name
    self._subscription_kind = subscription_kind
    self._trace_id = trace_id
    self._timeout = timeout

  def ticketize(self, operation_id, sequence_number, payload, complete):
    """See _Ticketizer.ticketize for specification."""
    if sequence_number:
      if complete:
        kind = interfaces.FrontToBackTicket.Kind.COMPLETION
      else:
        kind = interfaces.FrontToBackTicket.Kind.CONTINUATION
      return interfaces.FrontToBackTicket(
          operation_id, sequence_number, kind, self._name,
          self._subscription_kind, self._trace_id, payload, self._timeout)
    else:
      if complete:
        kind = interfaces.FrontToBackTicket.Kind.ENTIRE
      else:
        kind = interfaces.FrontToBackTicket.Kind.COMMENCEMENT
      return interfaces.FrontToBackTicket(
          operation_id, 0, kind, self._name, self._subscription_kind,
          self._trace_id, payload, self._timeout)

  def ticketize_abortion(self, operation_id, sequence_number, outcome):
    """See _Ticketizer.ticketize_abortion for specification."""
    if outcome in _FRONT_TO_BACK_NO_TRANSMISSION_OUTCOMES:
      return None
    else:
      kind = _ABORTION_OUTCOME_TO_FRONT_TO_BACK_TICKET_KIND[outcome]
      return interfaces.FrontToBackTicket(
          operation_id, sequence_number, kind, None, None, None, None, None)


class _BackTicketizer(_Ticketizer):
  """Back-side ticket-creating behavior."""

  def ticketize(self, operation_id, sequence_number, payload, complete):
    """See _Ticketizer.ticketize for specification."""
    if complete:
      kind = interfaces.BackToFrontTicket.Kind.COMPLETION
    else:
      kind = interfaces.BackToFrontTicket.Kind.CONTINUATION
    return interfaces.BackToFrontTicket(
        operation_id, sequence_number, kind, payload)

  def ticketize_abortion(self, operation_id, sequence_number, outcome):
    """See _Ticketizer.ticketize_abortion for specification."""
    if outcome in _BACK_TO_FRONT_NO_TRANSMISSION_OUTCOMES:
      return None
    else:
      kind = _ABORTION_OUTCOME_TO_BACK_TO_FRONT_TICKET_KIND[outcome]
      return interfaces.BackToFrontTicket(
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

  def abort(self, outcome):
    """See _interfaces.TransmissionManager.abort for specification."""


class _TransmittingTransmissionManager(TransmissionManager):
  """A TransmissionManager implementation that sends tickets."""

  def __init__(
      self, lock, pool, callback, operation_id, ticketizer,
      termination_manager):
    """Constructor.

    Args:
      lock: The operation-servicing-wide lock object.
      pool: A thread pool in which the work of transmitting tickets will be
        performed.
      callback: A callable that accepts tickets and sends them to the other side
        of the operation.
      operation_id: The operation's ID.
      ticketizer: A _Ticketizer for ticket creation.
      termination_manager: The _interfaces.TerminationManager associated with
        this operation.
    """
    self._lock = lock
    self._pool = pool
    self._callback = callback
    self._operation_id = operation_id
    self._ticketizer = ticketizer
    self._termination_manager = termination_manager
    self._ingestion_manager = None
    self._expiration_manager = None

    self._emissions = []
    self._emission_complete = False
    self._outcome = None
    self._lowest_unused_sequence_number = 0
    self._transmitting = False

  def set_ingestion_and_expiration_managers(
      self, ingestion_manager, expiration_manager):
    """See overridden method for specification."""
    self._ingestion_manager = ingestion_manager
    self._expiration_manager = expiration_manager

  def _lead_ticket(self, emission, complete):
    """Creates a ticket suitable for leading off the transmission loop.

    Args:
      emission: A customer payload object to be sent to the other side of the
        operation.
      complete: Whether or not the sequence of customer payloads ends with
        the passed object.

    Returns:
      A ticket with which to lead off the transmission loop.
    """
    sequence_number = self._lowest_unused_sequence_number
    self._lowest_unused_sequence_number += 1
    return self._ticketizer.ticketize(
        self._operation_id, sequence_number, emission, complete)

  def _abortive_response_ticket(self, outcome):
    """Creates a ticket indicating operation abortion.

    Args:
      outcome: An interfaces.Outcome value describing operation abortion.

    Returns:
      A ticket indicating operation abortion.
    """
    ticket = self._ticketizer.ticketize_abortion(
        self._operation_id, self._lowest_unused_sequence_number, outcome)
    if ticket is None:
      return None
    else:
      self._lowest_unused_sequence_number += 1
      return ticket

  def _next_ticket(self):
    """Creates the next ticket to be sent to the other side of the operation.

    Returns:
      A (completed, ticket) tuple comprised of a boolean indicating whether or
        not the sequence of tickets has completed normally and a ticket to send
        to the other side if the sequence of tickets hasn't completed. The tuple
        will never have both a True first element and a non-None second element.
    """
    if self._emissions is None:
      return False, None
    elif self._outcome is None:
      if self._emissions:
        payload = self._emissions.pop(0)
        complete = self._emission_complete and not self._emissions
        sequence_number = self._lowest_unused_sequence_number
        self._lowest_unused_sequence_number += 1
        return complete, self._ticketizer.ticketize(
            self._operation_id, sequence_number, payload, complete)
      else:
        return self._emission_complete, None
    else:
      ticket = self._abortive_response_ticket(self._outcome)
      self._emissions = None
      return False, None if ticket is None else ticket

  def _transmit(self, ticket):
    """Commences the transmission loop sending tickets.

    Args:
      ticket: A ticket to be sent to the other side of the operation.
    """
    def transmit(ticket):
      while True:
        transmission_outcome = callable_util.call_logging_exceptions(
            self._callback, _TRANSMISSION_EXCEPTION_LOG_MESSAGE, ticket)
        if transmission_outcome.exception is None:
          with self._lock:
            complete, ticket = self._next_ticket()
            if ticket is None:
              if complete:
                self._termination_manager.transmission_complete()
              self._transmitting = False
              return
        else:
          with self._lock:
            self._emissions = None
            self._termination_manager.abort(
                interfaces.Outcome.TRANSMISSION_FAILURE)
            self._ingestion_manager.abort()
            self._expiration_manager.abort()
            self._transmitting = False
            return

    self._pool.submit(callable_util.with_exceptions_logged(
        transmit, _constants.INTERNAL_ERROR_LOG_MESSAGE), ticket)
    self._transmitting = True

  def inmit(self, emission, complete):
    """See _interfaces.TransmissionManager.inmit for specification."""
    if self._emissions is not None and self._outcome is None:
      self._emission_complete = complete
      if self._transmitting:
        self._emissions.append(emission)
      else:
        self._transmit(self._lead_ticket(emission, complete))

  def abort(self, outcome):
    """See _interfaces.TransmissionManager.abort for specification."""
    if self._emissions is not None and self._outcome is None:
      self._outcome = outcome
      if not self._transmitting:
        ticket = self._abortive_response_ticket(outcome)
        self._emissions = None
        if ticket is not None:
          self._transmit(ticket)


def front_transmission_manager(
    lock, pool, callback, operation_id, name, subscription_kind, trace_id,
    timeout, termination_manager):
  """Creates a TransmissionManager appropriate for front-side use.

  Args:
    lock: The operation-servicing-wide lock object.
    pool: A thread pool in which the work of transmitting tickets will be
      performed.
    callback: A callable that accepts tickets and sends them to the other side
      of the operation.
    operation_id: The operation's ID.
    name: The name of the operation.
    subscription_kind: An interfaces.ServicedSubscription.Kind value
      describing the interest the front has in tickets sent from the back.
    trace_id: A uuid.UUID identifying a set of related operations to which
      this operation belongs.
    timeout: A length of time in seconds to allow for the entire operation.
    termination_manager: The _interfaces.TerminationManager associated with
      this operation.

  Returns:
    A TransmissionManager appropriate for front-side use.
  """
  return _TransmittingTransmissionManager(
      lock, pool, callback, operation_id, _FrontTicketizer(
          name, subscription_kind, trace_id, timeout),
      termination_manager)


def back_transmission_manager(
    lock, pool, callback, operation_id, termination_manager,
    subscription_kind):
  """Creates a TransmissionManager appropriate for back-side use.

  Args:
    lock: The operation-servicing-wide lock object.
    pool: A thread pool in which the work of transmitting tickets will be
      performed.
    callback: A callable that accepts tickets and sends them to the other side
      of the operation.
    operation_id: The operation's ID.
    termination_manager: The _interfaces.TerminationManager associated with
      this operation.
    subscription_kind: An interfaces.ServicedSubscription.Kind value
      describing the interest the front has in tickets sent from the back.

  Returns:
    A TransmissionManager appropriate for back-side use.
  """
  if subscription_kind is interfaces.ServicedSubscription.Kind.NONE:
    return _EmptyTransmissionManager()
  else:
    return _TransmittingTransmissionManager(
        lock, pool, callback, operation_id, _BackTicketizer(),
        termination_manager)
