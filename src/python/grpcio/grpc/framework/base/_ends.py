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

"""Implementations of FrontLinks and BackLinks."""

import collections
import threading
import uuid

# _interfaces is referenced from specification in this module.
from grpc.framework.base import _cancellation
from grpc.framework.base import _context
from grpc.framework.base import _emission
from grpc.framework.base import _expiration
from grpc.framework.base import _ingestion
from grpc.framework.base import _interfaces  # pylint: disable=unused-import
from grpc.framework.base import _reception
from grpc.framework.base import _termination
from grpc.framework.base import _transmission
from grpc.framework.base import interfaces
from grpc.framework.foundation import callable_util

_IDLE_ACTION_EXCEPTION_LOG_MESSAGE = 'Exception calling idle action!'


class _EasyOperation(interfaces.Operation):
  """A trivial implementation of interfaces.Operation."""

  def __init__(self, emission_manager, context, cancellation_manager):
    """Constructor.

    Args:
      emission_manager: The _interfaces.EmissionManager for the operation that
        will accept values emitted by customer code.
      context: The interfaces.OperationContext for use by the customer
        during the operation.
      cancellation_manager: The _interfaces.CancellationManager for the
        operation.
    """
    self.consumer = emission_manager
    self.context = context
    self._cancellation_manager = cancellation_manager

  def cancel(self):
    self._cancellation_manager.cancel()


class _Endlette(object):
  """Utility for stateful behavior common to Fronts and Backs."""

  def __init__(self, pool):
    """Constructor.

    Args:
      pool: A thread pool to use when calling registered idle actions.
    """
    self._lock = threading.Lock()
    self._pool = pool
    # Dictionary from operation IDs to ReceptionManager-or-None. A None value
    # indicates an in-progress fire-and-forget operation for which the customer
    # has chosen to ignore results.
    self._operations = {}
    self._stats = {outcome: 0 for outcome in interfaces.Outcome}
    self._idle_actions = []

  def terminal_action(self, operation_id):
    """Constructs the termination action for a single operation.

    Args:
      operation_id: An operation ID.

    Returns:
      A callable that takes an operation outcome for an argument to be used as
        the termination action for the operation associated with the given
        operation ID.
    """
    def termination_action(outcome):
      with self._lock:
        self._stats[outcome] += 1
        self._operations.pop(operation_id, None)
        if not self._operations:
          for action in self._idle_actions:
            self._pool.submit(callable_util.with_exceptions_logged(
                action, _IDLE_ACTION_EXCEPTION_LOG_MESSAGE))
          self._idle_actions = []
    return termination_action

  def __enter__(self):
    self._lock.acquire()

  def __exit__(self, exc_type, exc_val, exc_tb):
    self._lock.release()

  def get_operation(self, operation_id):
    return self._operations.get(operation_id, None)

  def add_operation(self, operation_id, operation_reception_manager):
    self._operations[operation_id] = operation_reception_manager

  def operation_stats(self):
    with self._lock:
      return dict(self._stats)

  def add_idle_action(self, action):
    with self._lock:
      if self._operations:
        self._idle_actions.append(action)
      else:
        self._pool.submit(callable_util.with_exceptions_logged(
            action, _IDLE_ACTION_EXCEPTION_LOG_MESSAGE))


class _FrontManagement(
    collections.namedtuple(
        '_FrontManagement',
        ('reception', 'emission', 'operation', 'cancellation'))):
  """Just a trivial helper class to bundle four fellow-traveling objects."""


def _front_operate(
    callback, work_pool, transmission_pool, utility_pool,
    termination_action, operation_id, name, payload, complete, timeout,
    subscription, trace_id):
  """Constructs objects necessary for front-side operation management.

  Args:
    callback: A callable that accepts interfaces.FrontToBackTickets and
      delivers them to the other side of the operation. Execution of this
      callable may take any arbitrary length of time.
    work_pool: A thread pool in which to execute customer code.
    transmission_pool: A thread pool to use for transmitting to the other side
      of the operation.
    utility_pool: A thread pool for utility tasks.
    termination_action: A no-arg behavior to be called upon operation
      completion.
    operation_id: An object identifying the operation.
    name: The name of the method being called during the operation.
    payload: The first customer-significant value to be transmitted to the other
      side. May be None if there is no such value or if the customer chose not
      to pass it at operation invocation.
    complete: A boolean indicating whether or not additional payloads will be
      supplied by the customer.
    timeout: A length of time in seconds to allow for the operation.
    subscription: A interfaces.ServicedSubscription describing the
      customer's interest in the results of the operation.
    trace_id: A uuid.UUID identifying a set of related operations to which this
      operation belongs. May be None.

  Returns:
    A _FrontManagement object bundling together the
      _interfaces.ReceptionManager, _interfaces.EmissionManager,
      _context.OperationContext, and _interfaces.CancellationManager for the
      operation.
  """
  lock = threading.Lock()
  with lock:
    termination_manager = _termination.front_termination_manager(
        work_pool, utility_pool, termination_action, subscription.kind)
    transmission_manager = _transmission.front_transmission_manager(
        lock, transmission_pool, callback, operation_id, name,
        subscription.kind, trace_id, timeout, termination_manager)
    operation_context = _context.OperationContext(
        lock, operation_id, interfaces.Outcome.SERVICED_FAILURE,
        termination_manager, transmission_manager)
    emission_manager = _emission.front_emission_manager(
        lock, termination_manager, transmission_manager)
    ingestion_manager = _ingestion.front_ingestion_manager(
        lock, work_pool, subscription, termination_manager,
        transmission_manager, operation_context)
    expiration_manager = _expiration.front_expiration_manager(
        lock, termination_manager, transmission_manager, ingestion_manager,
        timeout)
    reception_manager = _reception.front_reception_manager(
        lock, termination_manager, transmission_manager, ingestion_manager,
        expiration_manager)
    cancellation_manager = _cancellation.CancellationManager(
        lock, termination_manager, transmission_manager, ingestion_manager,
        expiration_manager)

    termination_manager.set_expiration_manager(expiration_manager)
    transmission_manager.set_ingestion_and_expiration_managers(
        ingestion_manager, expiration_manager)
    operation_context.set_ingestion_and_expiration_managers(
        ingestion_manager, expiration_manager)
    emission_manager.set_ingestion_manager_and_expiration_manager(
        ingestion_manager, expiration_manager)
    ingestion_manager.set_expiration_manager(expiration_manager)

    transmission_manager.inmit(payload, complete)

    if subscription.kind is interfaces.ServicedSubscription.Kind.NONE:
      returned_reception_manager = None
    else:
      returned_reception_manager = reception_manager

    return _FrontManagement(
        returned_reception_manager, emission_manager, operation_context,
        cancellation_manager)


class FrontLink(interfaces.FrontLink):
  """An implementation of interfaces.FrontLink."""

  def __init__(self, work_pool, transmission_pool, utility_pool):
    """Constructor.

    Args:
      work_pool: A thread pool to be used for executing customer code.
      transmission_pool: A thread pool to be used for transmitting values to
        the other side of the operation.
      utility_pool: A thread pool to be used for utility tasks.
    """
    self._endlette = _Endlette(utility_pool)
    self._work_pool = work_pool
    self._transmission_pool = transmission_pool
    self._utility_pool = utility_pool
    self._callback = None

    self._operations = {}

  def join_rear_link(self, rear_link):
    """See interfaces.ForeLink.join_rear_link for specification."""
    with self._endlette:
      self._callback = rear_link.accept_front_to_back_ticket

  def operation_stats(self):
    """See interfaces.End.operation_stats for specification."""
    return self._endlette.operation_stats()

  def add_idle_action(self, action):
    """See interfaces.End.add_idle_action for specification."""
    self._endlette.add_idle_action(action)

  def operate(
      self, name, payload, complete, timeout, subscription, trace_id):
    """See interfaces.Front.operate for specification."""
    operation_id = uuid.uuid4()
    with self._endlette:
      management = _front_operate(
          self._callback, self._work_pool, self._transmission_pool,
          self._utility_pool, self._endlette.terminal_action(operation_id),
          operation_id, name, payload, complete, timeout, subscription,
          trace_id)
      self._endlette.add_operation(operation_id, management.reception)
      return _EasyOperation(
          management.emission, management.operation, management.cancellation)

  def accept_back_to_front_ticket(self, ticket):
    """See interfaces.End.act for specification."""
    with self._endlette:
      reception_manager = self._endlette.get_operation(ticket.operation_id)
    if reception_manager:
      reception_manager.receive_ticket(ticket)


def _back_operate(
    servicer, callback, work_pool, transmission_pool, utility_pool,
    termination_action, ticket, default_timeout, maximum_timeout):
  """Constructs objects necessary for back-side operation management.

  Also begins back-side operation by feeding the first received ticket into the
  constructed _interfaces.ReceptionManager.

  Args:
    servicer: An interfaces.Servicer for servicing operations.
    callback: A callable that accepts interfaces.BackToFrontTickets and
      delivers them to the other side of the operation. Execution of this
      callable may take any arbitrary length of time.
    work_pool: A thread pool in which to execute customer code.
    transmission_pool: A thread pool to use for transmitting to the other side
      of the operation.
    utility_pool: A thread pool for utility tasks.
    termination_action: A no-arg behavior to be called upon operation
      completion.
    ticket: The first interfaces.FrontToBackTicket received for the operation.
    default_timeout: A length of time in seconds to be used as the default
      time alloted for a single operation.
    maximum_timeout: A length of time in seconds to be used as the maximum
      time alloted for a single operation.

  Returns:
    The _interfaces.ReceptionManager to be used for the operation.
  """
  lock = threading.Lock()
  with lock:
    termination_manager = _termination.back_termination_manager(
        work_pool, utility_pool, termination_action, ticket.subscription)
    transmission_manager = _transmission.back_transmission_manager(
        lock, transmission_pool, callback, ticket.operation_id,
        termination_manager, ticket.subscription)
    operation_context = _context.OperationContext(
        lock, ticket.operation_id, interfaces.Outcome.SERVICER_FAILURE,
        termination_manager, transmission_manager)
    emission_manager = _emission.back_emission_manager(
        lock, termination_manager, transmission_manager)
    ingestion_manager = _ingestion.back_ingestion_manager(
        lock, work_pool, servicer, termination_manager,
        transmission_manager, operation_context, emission_manager)
    expiration_manager = _expiration.back_expiration_manager(
        lock, termination_manager, transmission_manager, ingestion_manager,
        ticket.timeout, default_timeout, maximum_timeout)
    reception_manager = _reception.back_reception_manager(
        lock, termination_manager, transmission_manager, ingestion_manager,
        expiration_manager)

    termination_manager.set_expiration_manager(expiration_manager)
    transmission_manager.set_ingestion_and_expiration_managers(
        ingestion_manager, expiration_manager)
    operation_context.set_ingestion_and_expiration_managers(
        ingestion_manager, expiration_manager)
    emission_manager.set_ingestion_manager_and_expiration_manager(
        ingestion_manager, expiration_manager)
    ingestion_manager.set_expiration_manager(expiration_manager)

  reception_manager.receive_ticket(ticket)

  return reception_manager


class BackLink(interfaces.BackLink):
  """An implementation of interfaces.BackLink."""

  def __init__(
      self, servicer, work_pool, transmission_pool, utility_pool,
      default_timeout, maximum_timeout):
    """Constructor.

    Args:
      servicer: An interfaces.Servicer for servicing operations.
      work_pool: A thread pool in which to execute customer code.
      transmission_pool: A thread pool to use for transmitting to the other side
        of the operation.
      utility_pool: A thread pool for utility tasks.
      default_timeout: A length of time in seconds to be used as the default
        time alloted for a single operation.
      maximum_timeout: A length of time in seconds to be used as the maximum
        time alloted for a single operation.
    """
    self._endlette = _Endlette(utility_pool)
    self._servicer = servicer
    self._work_pool = work_pool
    self._transmission_pool = transmission_pool
    self._utility_pool = utility_pool
    self._default_timeout = default_timeout
    self._maximum_timeout = maximum_timeout
    self._callback = None

  def join_fore_link(self, fore_link):
    """See interfaces.RearLink.join_fore_link for specification."""
    with self._endlette:
      self._callback = fore_link.accept_back_to_front_ticket

  def accept_front_to_back_ticket(self, ticket):
    """See interfaces.RearLink.accept_front_to_back_ticket for specification."""
    with self._endlette:
      reception_manager = self._endlette.get_operation(ticket.operation_id)
      if reception_manager is None:
        reception_manager = _back_operate(
            self._servicer, self._callback, self._work_pool,
            self._transmission_pool, self._utility_pool,
            self._endlette.terminal_action(ticket.operation_id), ticket,
            self._default_timeout, self._maximum_timeout)
        self._endlette.add_operation(ticket.operation_id, reception_manager)
      else:
        reception_manager.receive_ticket(ticket)

  def operation_stats(self):
    """See interfaces.End.operation_stats for specification."""
    return self._endlette.operation_stats()

  def add_idle_action(self, action):
    """See interfaces.End.add_idle_action for specification."""
    self._endlette.add_idle_action(action)
