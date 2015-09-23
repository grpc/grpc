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

"""State and behavior for ingestion during an operation."""

import abc
import collections
import enum

from grpc.framework.core import _constants
from grpc.framework.core import _interfaces
from grpc.framework.core import _utilities
from grpc.framework.foundation import abandonment
from grpc.framework.foundation import callable_util
from grpc.framework.interfaces.base import base

_CREATE_SUBSCRIPTION_EXCEPTION_LOG_MESSAGE = 'Exception initializing ingestion!'
_INGESTION_EXCEPTION_LOG_MESSAGE = 'Exception during ingestion!'


class _SubscriptionCreation(
    collections.namedtuple(
        '_SubscriptionCreation',
        ('kind', 'subscription', 'code', 'details',))):
  """A sum type for the outcome of ingestion initialization.

  Attributes:
    kind: A Kind value coarsely indicating how subscription creation completed.
    subscription: The created subscription. Only present if kind is
      Kind.SUBSCRIPTION.
    code: A code value to be sent to the other side of the operation along with
      an indication that the operation is being aborted due to an error on the
      remote side of the operation. Only present if kind is Kind.REMOTE_ERROR.
    details: A details value to be sent to the other side of the operation
      along with an indication that the operation is being aborted due to an
      error on the remote side of the operation. Only present if kind is
      Kind.REMOTE_ERROR.
  """

  @enum.unique
  class Kind(enum.Enum):
    SUBSCRIPTION = 'subscription'
    REMOTE_ERROR = 'remote error'
    ABANDONED = 'abandoned'


class _SubscriptionCreator(object):
  """Common specification of subscription-creating behavior."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def create(self, group, method):
    """Creates the base.Subscription of the local customer.

    Any exceptions raised by this method should be attributed to and treated as
    defects in the customer code called by this method.

    Args:
      group: The group identifier of the operation.
      method: The method identifier of the operation.

    Returns:
      A _SubscriptionCreation describing the result of subscription creation.
    """
    raise NotImplementedError()


class _ServiceSubscriptionCreator(_SubscriptionCreator):
  """A _SubscriptionCreator appropriate for service-side use."""

  def __init__(self, servicer, operation_context, output_operator):
    """Constructor.

    Args:
      servicer: The base.Servicer that will service the operation.
      operation_context: A base.OperationContext for the operation to be passed
        to the customer.
      output_operator: A base.Operator for the operation to be passed to the
        customer and to be called by the customer to accept operation data
        emitted by the customer.
    """
    self._servicer = servicer
    self._operation_context = operation_context
    self._output_operator = output_operator

  def create(self, group, method):
    try:
      subscription = self._servicer.service(
          group, method, self._operation_context, self._output_operator)
    except base.NoSuchMethodError as e:
      return _SubscriptionCreation(
          _SubscriptionCreation.Kind.REMOTE_ERROR, None, e.code, e.details)
    except abandonment.Abandoned:
      return _SubscriptionCreation(
          _SubscriptionCreation.Kind.ABANDONED, None, None, None)
    else:
      return _SubscriptionCreation(
          _SubscriptionCreation.Kind.SUBSCRIPTION, subscription, None, None)


def _wrap(behavior):
  def wrapped(*args, **kwargs):
    try:
      behavior(*args, **kwargs)
    except abandonment.Abandoned:
      return False
    else:
      return True
  return wrapped


class _IngestionManager(_interfaces.IngestionManager):
  """An implementation of _interfaces.IngestionManager."""

  def __init__(
      self, lock, pool, subscription, subscription_creator, termination_manager,
      transmission_manager, expiration_manager, protocol_manager):
    """Constructor.

    Args:
      lock: The operation-wide lock.
      pool: A thread pool in which to execute customer code.
      subscription: A base.Subscription describing the customer's interest in
        operation values from the other side. May be None if
        subscription_creator is not None.
      subscription_creator: A _SubscriptionCreator wrapping the portion of
        customer code that when called returns the base.Subscription describing
        the customer's interest in operation values from the other side. May be
        None if subscription is not None.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
      expiration_manager: The _interfaces.ExpirationManager for the operation.
      protocol_manager: The _interfaces.ProtocolManager for the operation.
    """
    self._lock = lock
    self._pool = pool
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._expiration_manager = expiration_manager
    self._protocol_manager = protocol_manager

    if subscription is None:
      self._subscription_creator = subscription_creator
      self._wrapped_operator = None
    elif subscription.kind is base.Subscription.Kind.FULL:
      self._subscription_creator = None
      self._wrapped_operator = _wrap(subscription.operator.advance)
    else:
      # TODO(nathaniel): Support other subscriptions.
      raise ValueError('Unsupported subscription "%s"!' % subscription.kind)
    self._pending_initial_metadata = None
    self._pending_payloads = []
    self._pending_completion = None
    self._local_allowance = 1
    # A nonnegative integer or None, with None indicating that the local
    # customer is done emitting anyway so there's no need to bother it by
    # informing it that the remote customer has granted it further permission to
    # emit.
    self._remote_allowance = 0
    self._processing = False

  def _abort_internal_only(self):
    self._subscription_creator = None
    self._wrapped_operator = None
    self._pending_initial_metadata = None
    self._pending_payloads = None
    self._pending_completion = None

  def _abort_and_notify(self, outcome_kind, code, details):
    self._abort_internal_only()
    if self._termination_manager.outcome is None:
      outcome = _utilities.Outcome(outcome_kind, code, details)
      self._termination_manager.abort(outcome)
      self._transmission_manager.abort(outcome)
      self._expiration_manager.terminate()

  def _operator_next(self):
    """Computes the next step for full-subscription ingestion.

    Returns:
      An initial_metadata, payload, completion, allowance, continue quintet
        indicating what operation values (if any) are available to pass into
        customer code and whether or not there is anything immediately
        actionable to call customer code to do.
    """
    if self._wrapped_operator is None:
      return None, None, None, None, False
    else:
      initial_metadata, payload, completion, allowance, action = [None] * 5
      if self._pending_initial_metadata is not None:
        initial_metadata = self._pending_initial_metadata
        self._pending_initial_metadata = None
        action = True
      if self._pending_payloads and 0 < self._local_allowance:
        payload = self._pending_payloads.pop(0)
        self._local_allowance -= 1
        action = True
      if not self._pending_payloads and self._pending_completion is not None:
        completion = self._pending_completion
        self._pending_completion = None
        action = True
      if self._remote_allowance is not None and 0 < self._remote_allowance:
        allowance = self._remote_allowance
        self._remote_allowance = 0
        action = True
      return initial_metadata, payload, completion, allowance, bool(action)

  def _operator_process(
      self, wrapped_operator, initial_metadata, payload,
      completion, allowance):
    while True:
      advance_outcome = callable_util.call_logging_exceptions(
          wrapped_operator, _INGESTION_EXCEPTION_LOG_MESSAGE,
          initial_metadata=initial_metadata, payload=payload,
          completion=completion, allowance=allowance)
      if advance_outcome.exception is None:
        if advance_outcome.return_value:
          with self._lock:
            if self._termination_manager.outcome is not None:
              return
            if completion is not None:
              self._termination_manager.ingestion_complete()
            initial_metadata, payload, completion, allowance, moar = (
                self._operator_next())
            if not moar:
              self._processing = False
              return
        else:
          with self._lock:
            if self._termination_manager.outcome is None:
              self._abort_and_notify(
                  base.Outcome.Kind.LOCAL_FAILURE, None, None)
            return
      else:
        with self._lock:
          if self._termination_manager.outcome is None:
            self._abort_and_notify(base.Outcome.Kind.LOCAL_FAILURE, None, None)
          return

  def _operator_post_create(self, subscription):
    wrapped_operator = _wrap(subscription.operator.advance)
    with self._lock:
      if self._termination_manager.outcome is not None:
        return
      self._wrapped_operator = wrapped_operator
      self._subscription_creator = None
      metadata, payload, completion, allowance, moar = self._operator_next()
      if not moar:
        self._processing = False
        return
    self._operator_process(
        wrapped_operator, metadata, payload, completion, allowance)

  def _create(self, subscription_creator, group, name):
    outcome = callable_util.call_logging_exceptions(
        subscription_creator.create,
        _CREATE_SUBSCRIPTION_EXCEPTION_LOG_MESSAGE, group, name)
    if outcome.return_value is None:
      with self._lock:
        if self._termination_manager.outcome is None:
          self._abort_and_notify(base.Outcome.Kind.LOCAL_FAILURE, None, None)
    elif outcome.return_value.kind is _SubscriptionCreation.Kind.ABANDONED:
      with self._lock:
        if self._termination_manager.outcome is None:
          self._abort_and_notify(base.Outcome.Kind.LOCAL_FAILURE, None, None)
    elif outcome.return_value.kind is _SubscriptionCreation.Kind.REMOTE_ERROR:
      code = outcome.return_value.code
      details = outcome.return_value.details
      with self._lock:
        if self._termination_manager.outcome is None:
          self._abort_and_notify(
              base.Outcome.Kind.REMOTE_FAILURE, code, details)
    elif outcome.return_value.subscription.kind is base.Subscription.Kind.FULL:
      self._protocol_manager.set_protocol_receiver(
          outcome.return_value.subscription.protocol_receiver)
      self._operator_post_create(outcome.return_value.subscription)
    else:
      # TODO(nathaniel): Support other subscriptions.
      raise ValueError(
          'Unsupported "%s"!' % outcome.return_value.subscription.kind)

  def _store_advance(self, initial_metadata, payload, completion, allowance):
    if initial_metadata is not None:
      self._pending_initial_metadata = initial_metadata
    if payload is not None:
      self._pending_payloads.append(payload)
    if completion is not None:
      self._pending_completion = completion
    if allowance is not None and self._remote_allowance is not None:
      self._remote_allowance += allowance

  def _operator_advance(self, initial_metadata, payload, completion, allowance):
    if self._processing:
      self._store_advance(initial_metadata, payload, completion, allowance)
    else:
      action = False
      if initial_metadata is not None:
        action = True
      if payload is not None:
        if 0 < self._local_allowance:
          self._local_allowance -= 1
          action = True
        else:
          self._pending_payloads.append(payload)
          payload = False
      if completion is not None:
        if self._pending_payloads:
          self._pending_completion = completion
        else:
          action = True
      if allowance is not None and self._remote_allowance is not None:
        allowance += self._remote_allowance
        self._remote_allowance = 0
        action = True
      if action:
        self._pool.submit(
            callable_util.with_exceptions_logged(
                self._operator_process, _constants.INTERNAL_ERROR_LOG_MESSAGE),
            self._wrapped_operator, initial_metadata, payload, completion,
            allowance)

  def set_group_and_method(self, group, method):
    """See _interfaces.IngestionManager.set_group_and_method for spec."""
    if self._subscription_creator is not None and not self._processing:
      self._pool.submit(
          callable_util.with_exceptions_logged(
              self._create, _constants.INTERNAL_ERROR_LOG_MESSAGE),
          self._subscription_creator, group, method)
      self._processing = True

  def add_local_allowance(self, allowance):
    """See _interfaces.IngestionManager.add_local_allowance for spec."""
    if any((self._subscription_creator, self._wrapped_operator,)):
      self._local_allowance += allowance
      if not self._processing:
        initial_metadata, payload, completion, allowance, moar = (
            self._operator_next())
        if moar:
          self._pool.submit(
              callable_util.with_exceptions_logged(
                  self._operator_process,
                  _constants.INTERNAL_ERROR_LOG_MESSAGE),
              initial_metadata, payload, completion, allowance)

  def local_emissions_done(self):
    self._remote_allowance = None

  def advance(self, initial_metadata, payload, completion, allowance):
    """See _interfaces.IngestionManager.advance for specification."""
    if self._subscription_creator is not None:
      self._store_advance(initial_metadata, payload, completion, allowance)
    elif self._wrapped_operator is not None:
      self._operator_advance(initial_metadata, payload, completion, allowance)


def invocation_ingestion_manager(
    subscription, lock, pool, termination_manager, transmission_manager,
    expiration_manager, protocol_manager):
  """Creates an IngestionManager appropriate for invocation-side use.

  Args:
    subscription: A base.Subscription indicating the customer's interest in the
      data and results from the service-side of the operation.
    lock: The operation-wide lock.
    pool: A thread pool in which to execute customer code.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the
      operation.
    expiration_manager: The _interfaces.ExpirationManager for the operation.
    protocol_manager: The _interfaces.ProtocolManager for the operation.

  Returns:
    An IngestionManager appropriate for invocation-side use.
  """
  return _IngestionManager(
      lock, pool, subscription, None, termination_manager, transmission_manager,
      expiration_manager, protocol_manager)


def service_ingestion_manager(
    servicer, operation_context, output_operator, lock, pool,
    termination_manager, transmission_manager, expiration_manager,
    protocol_manager):
  """Creates an IngestionManager appropriate for service-side use.

  The returned IngestionManager will require its set_group_and_name method to be
  called before its advance method may be called.

  Args:
    servicer: A base.Servicer for servicing the operation.
    operation_context: A base.OperationContext for the operation to be passed to
      the customer.
    output_operator: A base.Operator for the operation to be passed to the
      customer and to be called by the customer to accept operation data output
      by the customer.
    lock: The operation-wide lock.
    pool: A thread pool in which to execute customer code.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the
      operation.
    expiration_manager: The _interfaces.ExpirationManager for the operation.
    protocol_manager: The _interfaces.ProtocolManager for the operation.

  Returns:
    An IngestionManager appropriate for service-side use.
  """
  subscription_creator = _ServiceSubscriptionCreator(
      servicer, operation_context, output_operator)
  return _IngestionManager(
      lock, pool, None, subscription_creator, termination_manager,
      transmission_manager, expiration_manager, protocol_manager)
