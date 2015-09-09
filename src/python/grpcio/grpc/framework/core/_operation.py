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

"""Implementation of operations."""

import threading

from grpc.framework.core import _context
from grpc.framework.core import _emission
from grpc.framework.core import _expiration
from grpc.framework.core import _ingestion
from grpc.framework.core import _interfaces
from grpc.framework.core import _protocol
from grpc.framework.core import _reception
from grpc.framework.core import _termination
from grpc.framework.core import _transmission
from grpc.framework.core import _utilities


class _EasyOperation(_interfaces.Operation):
  """A trivial implementation of interfaces.Operation."""

  def __init__(
      self, lock, termination_manager, transmission_manager, expiration_manager,
      context, operator, reception_manager):
    """Constructor.

    Args:
      lock: The operation-wide lock.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
      expiration_manager: The _interfaces.ExpirationManager for the operation.
      context: A base.OperationContext for use by the customer during the
        operation.
      operator: A base.Operator for use by the customer during the operation.
      reception_manager: The _interfaces.ReceptionManager for the operation.
    """
    self._lock = lock
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._expiration_manager = expiration_manager
    self._reception_manager = reception_manager

    self.context = context
    self.operator = operator

  def handle_ticket(self, ticket):
    with self._lock:
      self._reception_manager.receive_ticket(ticket)

  def abort(self, outcome_kind):
    with self._lock:
      if self._termination_manager.outcome is None:
        outcome = _utilities.Outcome(outcome_kind, None, None)
        self._termination_manager.abort(outcome)
        self._transmission_manager.abort(outcome)
        self._expiration_manager.terminate()


def invocation_operate(
    operation_id, group, method, subscription, timeout, protocol_options,
    initial_metadata, payload, completion, ticket_sink, termination_action,
    pool):
  """Constructs objects necessary for front-side operation management.

  Args:
    operation_id: An object identifying the operation.
    group: The group identifier of the operation.
    method: The method identifier of the operation.
    subscription: A base.Subscription describing the customer's interest in the
      results of the operation.
    timeout: A length of time in seconds to allow for the operation.
    protocol_options: A transport-specific, application-specific, and/or
      protocol-specific value relating to the invocation. May be None.
    initial_metadata: An initial metadata value to be sent to the other side of
      the operation. May be None if the initial metadata will be passed later or
      if there will be no initial metadata passed at all.
    payload: The first payload value to be transmitted to the other side. May be
      None if there is no such value or if the customer chose not to pass it at
      operation invocation.
    completion: A base.Completion value indicating the end of values passed to
      the other side of the operation.
    ticket_sink: A callable that accepts links.Tickets and delivers them to the
      other side of the operation.
    termination_action: A callable that accepts the outcome of the operation as
      a base.Outcome value to be called on operation completion.
    pool: A thread pool with which to do the work of the operation.

  Returns:
    An _interfaces.Operation for the operation.
  """
  lock = threading.Lock()
  with lock:
    termination_manager = _termination.invocation_termination_manager(
        termination_action, pool)
    transmission_manager = _transmission.TransmissionManager(
        operation_id, ticket_sink, lock, pool, termination_manager)
    expiration_manager = _expiration.invocation_expiration_manager(
        timeout, lock, termination_manager, transmission_manager)
    protocol_manager = _protocol.invocation_protocol_manager(
        subscription, lock, pool, termination_manager, transmission_manager,
        expiration_manager)
    operation_context = _context.OperationContext(
        lock, termination_manager, transmission_manager, expiration_manager)
    emission_manager = _emission.EmissionManager(
        lock, termination_manager, transmission_manager, expiration_manager)
    ingestion_manager = _ingestion.invocation_ingestion_manager(
        subscription, lock, pool, termination_manager, transmission_manager,
        expiration_manager, protocol_manager)
    reception_manager = _reception.ReceptionManager(
        termination_manager, transmission_manager, expiration_manager,
        protocol_manager, ingestion_manager)

    termination_manager.set_expiration_manager(expiration_manager)
    transmission_manager.set_expiration_manager(expiration_manager)
    emission_manager.set_ingestion_manager(ingestion_manager)

    transmission_manager.kick_off(
        group, method, timeout, protocol_options, initial_metadata, payload,
        completion, None)

  return _EasyOperation(
      lock, termination_manager, transmission_manager, expiration_manager,
      operation_context, emission_manager, reception_manager)


def service_operate(
    servicer_package, ticket, ticket_sink, termination_action, pool):
  """Constructs an Operation for service of an operation.

  Args:
    servicer_package: A _utilities.ServicerPackage to be used servicing the
      operation.
    ticket: The first links.Ticket received for the operation.
    ticket_sink: A callable that accepts links.Tickets and delivers them to the
      other side of the operation.
    termination_action: A callable that accepts the outcome of the operation as
      a base.Outcome value to be called on operation completion.
    pool: A thread pool with which to do the work of the operation.

  Returns:
    An _interfaces.Operation for the operation.
  """
  lock = threading.Lock()
  with lock:
    termination_manager = _termination.service_termination_manager(
        termination_action, pool)
    transmission_manager = _transmission.TransmissionManager(
        ticket.operation_id, ticket_sink, lock, pool, termination_manager)
    expiration_manager = _expiration.service_expiration_manager(
        ticket.timeout, servicer_package.default_timeout,
        servicer_package.maximum_timeout, lock, termination_manager,
        transmission_manager)
    protocol_manager = _protocol.service_protocol_manager(
        lock, pool, termination_manager, transmission_manager,
        expiration_manager)
    operation_context = _context.OperationContext(
        lock, termination_manager, transmission_manager, expiration_manager)
    emission_manager = _emission.EmissionManager(
        lock, termination_manager, transmission_manager, expiration_manager)
    ingestion_manager = _ingestion.service_ingestion_manager(
        servicer_package.servicer, operation_context, emission_manager, lock,
        pool, termination_manager, transmission_manager, expiration_manager,
        protocol_manager)
    reception_manager = _reception.ReceptionManager(
        termination_manager, transmission_manager, expiration_manager,
        protocol_manager, ingestion_manager)

    termination_manager.set_expiration_manager(expiration_manager)
    transmission_manager.set_expiration_manager(expiration_manager)
    emission_manager.set_ingestion_manager(ingestion_manager)

    reception_manager.receive_ticket(ticket)

  return _EasyOperation(
      lock, termination_manager, transmission_manager, expiration_manager,
      operation_context, emission_manager, reception_manager)
