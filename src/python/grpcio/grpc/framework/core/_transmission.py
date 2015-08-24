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

from grpc.framework.core import _constants
from grpc.framework.core import _interfaces
from grpc.framework.foundation import callable_util
from grpc.framework.interfaces.base import base
from grpc.framework.interfaces.links import links

_TRANSMISSION_EXCEPTION_LOG_MESSAGE = 'Exception during transmission!'


def _explode_completion(completion):
  if completion is None:
    return None, None, None, None
  else:
    return (
        completion.terminal_metadata, completion.code, completion.message,
        links.Ticket.Termination.COMPLETION)


class TransmissionManager(_interfaces.TransmissionManager):
  """An _interfaces.TransmissionManager that sends links.Tickets."""

  def __init__(
      self, operation_id, ticket_sink, lock, pool, termination_manager):
    """Constructor.

    Args:
      operation_id: The operation's ID.
      ticket_sink: A callable that accepts tickets and sends them to the other
        side of the operation.
      lock: The operation-servicing-wide lock object.
      pool: A thread pool in which the work of transmitting tickets will be
        performed.
      termination_manager: The _interfaces.TerminationManager associated with
        this operation.
    """
    self._lock = lock
    self._pool = pool
    self._ticket_sink = ticket_sink
    self._operation_id = operation_id
    self._termination_manager = termination_manager
    self._expiration_manager = None

    self._lowest_unused_sequence_number = 0
    self._remote_allowance = 1
    self._remote_complete = False
    self._timeout = None
    self._local_allowance = 0
    self._initial_metadata = None
    self._payloads = []
    self._completion = None
    self._aborted = False
    self._abortion_outcome = None
    self._transmitting = False

  def set_expiration_manager(self, expiration_manager):
    """Sets the ExpirationManager with which this manager will cooperate."""
    self._expiration_manager = expiration_manager

  def _next_ticket(self):
    """Creates the next ticket to be transmitted.

    Returns:
      A links.Ticket to be sent to the other side of the operation or None if
        there is nothing to be sent at this time.
    """
    if self._aborted:
      if self._abortion_outcome is None:
        return None
      else:
        termination = _constants.ABORTION_OUTCOME_TO_TICKET_TERMINATION[
            self._abortion_outcome]
        if termination is None:
          return None
        else:
          self._abortion_outcome = None
          return links.Ticket(
              self._operation_id, self._lowest_unused_sequence_number, None,
              None, None, None, None, None, None, None, None, None,
              termination, None)

    action = False
    # TODO(nathaniel): Support other subscriptions.
    local_subscription = links.Ticket.Subscription.FULL
    timeout = self._timeout
    if timeout is not None:
      self._timeout = None
      action = True
    if self._local_allowance <= 0:
      allowance = None
    else:
      allowance = self._local_allowance
      self._local_allowance = 0
      action = True
    initial_metadata = self._initial_metadata
    if initial_metadata is not None:
      self._initial_metadata = None
      action = True
    if not self._payloads or self._remote_allowance <= 0:
      payload = None
    else:
      payload = self._payloads.pop(0)
      self._remote_allowance -= 1
      action = True
    if self._completion is None or self._payloads:
      terminal_metadata, code, message, termination = None, None, None, None
    else:
      terminal_metadata, code, message, termination = _explode_completion(
          self._completion)
      self._completion = None
      action = True

    if action:
      ticket = links.Ticket(
          self._operation_id, self._lowest_unused_sequence_number, None, None,
          local_subscription, timeout, allowance, initial_metadata, payload,
          terminal_metadata, code, message, termination, None)
      self._lowest_unused_sequence_number += 1
      return ticket
    else:
      return None

  def _transmit(self, ticket):
    """Commences the transmission loop sending tickets.

    Args:
      ticket: A links.Ticket to be sent to the other side of the operation.
    """
    def transmit(ticket):
      while True:
        transmission_outcome = callable_util.call_logging_exceptions(
            self._ticket_sink, _TRANSMISSION_EXCEPTION_LOG_MESSAGE, ticket)
        if transmission_outcome.exception is None:
          with self._lock:
            if ticket.termination is links.Ticket.Termination.COMPLETION:
              self._termination_manager.transmission_complete()
            ticket = self._next_ticket()
            if ticket is None:
              self._transmitting = False
              return
        else:
          with self._lock:
            if self._termination_manager.outcome is None:
              self._termination_manager.abort(base.Outcome.TRANSMISSION_FAILURE)
              self._expiration_manager.terminate()
            return

    self._pool.submit(callable_util.with_exceptions_logged(
        transmit, _constants.INTERNAL_ERROR_LOG_MESSAGE), ticket)
    self._transmitting = True

  def kick_off(
      self, group, method, timeout, initial_metadata, payload, completion,
      allowance):
    """See _interfaces.TransmissionManager.kickoff for specification."""
    # TODO(nathaniel): Support other subscriptions.
    subscription = links.Ticket.Subscription.FULL
    terminal_metadata, code, message, termination = _explode_completion(
        completion)
    self._remote_allowance = 1 if payload is None else 0
    ticket = links.Ticket(
        self._operation_id, 0, group, method, subscription, timeout, allowance,
        initial_metadata, payload, terminal_metadata, code, message,
        termination, None)
    self._lowest_unused_sequence_number = 1
    self._transmit(ticket)

  def advance(self, initial_metadata, payload, completion, allowance):
    """See _interfaces.TransmissionManager.advance for specification."""
    effective_initial_metadata = initial_metadata
    effective_payload = payload
    effective_completion = completion
    if allowance is not None and not self._remote_complete:
      effective_allowance = allowance
    else:
      effective_allowance = None
    if self._transmitting:
      if effective_initial_metadata is not None:
        self._initial_metadata = effective_initial_metadata
      if effective_payload is not None:
        self._payloads.append(effective_payload)
      if effective_completion is not None:
        self._completion = effective_completion
      if effective_allowance is not None:
        self._local_allowance += effective_allowance
    else:
      if effective_payload is not None:
        if 0 < self._remote_allowance:
          ticket_payload = effective_payload
          self._remote_allowance -= 1
        else:
          self._payloads.append(effective_payload)
          ticket_payload = None
      else:
        ticket_payload = None
      if effective_completion is not None and not self._payloads:
        ticket_completion = effective_completion
      else:
        self._completion = effective_completion
        ticket_completion = None
      if any(
          (effective_initial_metadata, ticket_payload, ticket_completion,
           effective_allowance)):
        terminal_metadata, code, message, termination = _explode_completion(
            completion)
        ticket = links.Ticket(
            self._operation_id, self._lowest_unused_sequence_number, None, None,
            None, None, allowance, effective_initial_metadata, ticket_payload,
            terminal_metadata, code, message, termination, None)
        self._lowest_unused_sequence_number += 1
        self._transmit(ticket)

  def timeout(self, timeout):
    """See _interfaces.TransmissionManager.timeout for specification."""
    if self._transmitting:
      self._timeout = timeout
    else:
      ticket = links.Ticket(
          self._operation_id, self._lowest_unused_sequence_number, None, None,
          None, timeout, None, None, None, None, None, None, None, None)
      self._lowest_unused_sequence_number += 1
      self._transmit(ticket)

  def allowance(self, allowance):
    """See _interfaces.TransmissionManager.allowance for specification."""
    if self._transmitting or not self._payloads:
      self._remote_allowance += allowance
    else:
      self._remote_allowance += allowance - 1
      payload = self._payloads.pop(0)
      if self._payloads:
        completion = None
      else:
        completion = self._completion
        self._completion = None
      terminal_metadata, code, message, termination = _explode_completion(
          completion)
      ticket = links.Ticket(
          self._operation_id, self._lowest_unused_sequence_number, None, None,
          None, None, None, None, payload, terminal_metadata, code, message,
          termination, None)
      self._lowest_unused_sequence_number += 1
      self._transmit(ticket)

  def remote_complete(self):
    """See _interfaces.TransmissionManager.remote_complete for specification."""
    self._remote_complete = True
    self._local_allowance = 0

  def abort(self, outcome):
    """See _interfaces.TransmissionManager.abort for specification."""
    if self._transmitting:
      self._aborted, self._abortion_outcome = True, outcome
    else:
      self._aborted = True
      if outcome is not None:
        termination = _constants.ABORTION_OUTCOME_TO_TICKET_TERMINATION[
            outcome]
        if termination is not None:
          ticket = links.Ticket(
              self._operation_id, self._lowest_unused_sequence_number, None,
              None, None, None, None, None, None, None, None, None,
              termination, None)
          self._transmit(ticket)
