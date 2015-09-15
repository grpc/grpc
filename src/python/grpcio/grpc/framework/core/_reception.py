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

from grpc.framework.core import _interfaces
from grpc.framework.core import _utilities
from grpc.framework.interfaces.base import base
from grpc.framework.interfaces.base import utilities
from grpc.framework.interfaces.links import links

_REMOTE_TICKET_TERMINATION_TO_LOCAL_OUTCOME_KIND = {
    links.Ticket.Termination.CANCELLATION: base.Outcome.Kind.CANCELLED,
    links.Ticket.Termination.EXPIRATION: base.Outcome.Kind.EXPIRED,
    links.Ticket.Termination.SHUTDOWN: base.Outcome.Kind.REMOTE_SHUTDOWN,
    links.Ticket.Termination.RECEPTION_FAILURE:
        base.Outcome.Kind.RECEPTION_FAILURE,
    links.Ticket.Termination.TRANSMISSION_FAILURE:
        base.Outcome.Kind.TRANSMISSION_FAILURE,
    links.Ticket.Termination.LOCAL_FAILURE: base.Outcome.Kind.REMOTE_FAILURE,
    links.Ticket.Termination.REMOTE_FAILURE: base.Outcome.Kind.LOCAL_FAILURE,
}

_RECEPTION_FAILURE_OUTCOME = _utilities.Outcome(
    base.Outcome.Kind.RECEPTION_FAILURE, None, None)


def _carrying_protocol_context(ticket):
  return ticket.protocol is not None and ticket.protocol.kind in (
      links.Protocol.Kind.INVOCATION_CONTEXT,
      links.Protocol.Kind.SERVICER_CONTEXT,)


class ReceptionManager(_interfaces.ReceptionManager):
  """A ReceptionManager based around a _Receiver passed to it."""

  def __init__(
      self, termination_manager, transmission_manager, expiration_manager,
      protocol_manager, ingestion_manager):
    """Constructor.

    Args:
      termination_manager: The operation's _interfaces.TerminationManager.
      transmission_manager: The operation's _interfaces.TransmissionManager.
      expiration_manager: The operation's _interfaces.ExpirationManager.
      protocol_manager: The operation's _interfaces.ProtocolManager.
      ingestion_manager: The operation's _interfaces.IngestionManager.
    """
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._expiration_manager = expiration_manager
    self._protocol_manager = protocol_manager
    self._ingestion_manager = ingestion_manager

    self._lowest_unseen_sequence_number = 0
    self._out_of_sequence_tickets = {}
    self._aborted = False

  def _abort(self, outcome):
    self._aborted = True
    if self._termination_manager.outcome is None:
      self._termination_manager.abort(outcome)
      self._transmission_manager.abort(None)
      self._expiration_manager.terminate()

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
    else:
      return False

  def _process_one(self, ticket):
    if ticket.sequence_number == 0:
      self._ingestion_manager.set_group_and_method(ticket.group, ticket.method)
      if _carrying_protocol_context(ticket):
        self._protocol_manager.accept_protocol_context(ticket.protocol.value)
      else:
        self._protocol_manager.accept_protocol_context(None)
    if ticket.timeout is not None:
      self._expiration_manager.change_timeout(ticket.timeout)
    if ticket.termination is None:
      completion = None
    else:
      completion = utilities.completion(
          ticket.terminal_metadata, ticket.code, ticket.message)
      self._termination_manager.reception_complete(ticket.code, ticket.message)
    self._ingestion_manager.advance(
        ticket.initial_metadata, ticket.payload, completion, ticket.allowance)
    if ticket.allowance is not None:
      self._transmission_manager.allowance(ticket.allowance)

  def _process(self, ticket):
    """Process those tickets ready to be processed.

    Args:
      ticket: A just-arrived ticket the sequence number of which matches this
        _ReceptionManager's _lowest_unseen_sequence_number field.
    """
    while True:
      self._process_one(ticket)
      next_ticket = self._out_of_sequence_tickets.pop(
          ticket.sequence_number + 1, None)
      if next_ticket is None:
        self._lowest_unseen_sequence_number = ticket.sequence_number + 1
        return
      else:
        ticket = next_ticket

  def receive_ticket(self, ticket):
    """See _interfaces.ReceptionManager.receive_ticket for specification."""
    if self._aborted:
      return
    elif self._sequence_failure(ticket):
      self._abort(_RECEPTION_FAILURE_OUTCOME)
    elif ticket.termination not in (None, links.Ticket.Termination.COMPLETION):
      outcome_kind = _REMOTE_TICKET_TERMINATION_TO_LOCAL_OUTCOME_KIND[
          ticket.termination]
      self._abort(
          _utilities.Outcome(outcome_kind, ticket.code, ticket.message))
    elif ticket.sequence_number == self._lowest_unseen_sequence_number:
      self._process(ticket)
    else:
      self._out_of_sequence_tickets[ticket.sequence_number] = ticket
