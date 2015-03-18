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

from grpc.framework.base import _constants
from grpc.framework.base import _interfaces
from grpc.framework.base import exceptions
from grpc.framework.base import interfaces
from grpc.framework.foundation import abandonment
from grpc.framework.foundation import callable_util
from grpc.framework.foundation import stream

_CREATE_CONSUMER_EXCEPTION_LOG_MESSAGE = 'Exception initializing ingestion!'
_CONSUME_EXCEPTION_LOG_MESSAGE = 'Exception during ingestion!'


class _ConsumerCreation(collections.namedtuple(
    '_ConsumerCreation', ('consumer', 'remote_error', 'abandoned'))):
  """A sum type for the outcome of ingestion initialization.

  Either consumer will be non-None, remote_error will be True, or abandoned will
  be True.

  Attributes:
    consumer: A stream.Consumer for ingesting payloads.
    remote_error: A boolean indicating that the consumer could not be created
      due to an error on the remote side of the operation.
    abandoned: A boolean indicating that the consumer creation was abandoned.
  """


class _EmptyConsumer(stream.Consumer):
  """A no-operative stream.Consumer that ignores all inputs and calls."""

  def consume(self, value):
    """See stream.Consumer.consume for specification."""

  def terminate(self):
    """See stream.Consumer.terminate for specification."""

  def consume_and_terminate(self, value):
    """See stream.Consumer.consume_and_terminate for specification."""


class _ConsumerCreator(object):
  """Common specification of different consumer-creating behavior."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def create_consumer(self, requirement):
    """Creates the stream.Consumer to which customer payloads will be delivered.

    Any exceptions raised by this method should be attributed to and treated as
    defects in the serviced or servicer code called by this method.

    Args:
      requirement: A value required by this _ConsumerCreator for consumer
        creation.

    Returns:
      A _ConsumerCreation describing the result of consumer creation.
    """
    raise NotImplementedError()


class _FrontConsumerCreator(_ConsumerCreator):
  """A _ConsumerCreator appropriate for front-side use."""

  def __init__(self, subscription, operation_context):
    """Constructor.

    Args:
      subscription: The serviced's interfaces.ServicedSubscription for the
        operation.
      operation_context: The interfaces.OperationContext object for the
        operation.
    """
    self._subscription = subscription
    self._operation_context = operation_context

  def create_consumer(self, requirement):
    """See _ConsumerCreator.create_consumer for specification."""
    if self._subscription.kind is interfaces.ServicedSubscription.Kind.FULL:
      try:
        return _ConsumerCreation(
            self._subscription.ingestor.consumer(self._operation_context),
            False, False)
      except abandonment.Abandoned:
        return _ConsumerCreation(None, False, True)
    else:
      return _ConsumerCreation(_EmptyConsumer(), False, False)


class _BackConsumerCreator(_ConsumerCreator):
  """A _ConsumerCreator appropriate for back-side use."""

  def __init__(self, servicer, operation_context, emission_consumer):
    """Constructor.

    Args:
      servicer: The interfaces.Servicer that will service the operation.
      operation_context: The interfaces.OperationContext object for the
        operation.
      emission_consumer: The stream.Consumer object to which payloads emitted
        from the operation will be passed.
    """
    self._servicer = servicer
    self._operation_context = operation_context
    self._emission_consumer = emission_consumer

  def create_consumer(self, requirement):
    """See _ConsumerCreator.create_consumer for full specification.

    Args:
      requirement: The name of the Servicer method to be called during this
        operation.

    Returns:
      A _ConsumerCreation describing the result of consumer creation.
    """
    try:
      return _ConsumerCreation(
          self._servicer.service(
              requirement, self._operation_context, self._emission_consumer),
          False, False)
    except exceptions.NoSuchMethodError:
      return _ConsumerCreation(None, True, False)
    except abandonment.Abandoned:
      return _ConsumerCreation(None, False, True)


class _WrappedConsumer(object):
  """Wraps a consumer to catch the exceptions that it is allowed to throw."""

  def __init__(self, consumer):
    """Constructor.

    Args:
      consumer: A stream.Consumer that may raise abandonment.Abandoned from any
        of its methods.
    """
    self._consumer = consumer

  def moar(self, payload, complete):
    """Makes progress with the wrapped consumer.

    This method catches all exceptions allowed to be thrown by the wrapped
    consumer. Any exceptions raised by this method should be blamed on the
    customer-supplied consumer.

    Args:
      payload: A customer-significant payload object. May be None only if
        complete is True.
      complete: Whether or not the end of the payload sequence has been reached.
        Must be True if payload is None.

    Returns:
      True if the wrapped consumer made progress or False if the wrapped
        consumer raised abandonment.Abandoned to indicate its abandonment of
        progress.
    """
    try:
      if payload is None:
        self._consumer.terminate()
      elif complete:
        self._consumer.consume_and_terminate(payload)
      else:
        self._consumer.consume(payload)
      return True
    except abandonment.Abandoned:
      return False


class _IngestionManager(_interfaces.IngestionManager):
  """An implementation of _interfaces.IngestionManager."""

  def __init__(
      self, lock, pool, consumer_creator, failure_outcome, termination_manager,
      transmission_manager):
    """Constructor.

    Args:
      lock: The operation-wide lock.
      pool: A thread pool in which to execute customer code.
      consumer_creator: A _ConsumerCreator wrapping the portion of customer code
        that when called returns the stream.Consumer with which the customer
        code will ingest payload values.
      failure_outcome: Whichever one of
        interfaces.Outcome.SERVICED_FAILURE or
        interfaces.Outcome.SERVICER_FAILURE describes local failure of
        customer code.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
    """
    self._lock = lock
    self._pool = pool
    self._consumer_creator = consumer_creator
    self._failure_outcome = failure_outcome
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._expiration_manager = None

    self._wrapped_ingestion_consumer = None
    self._pending_ingestion = []
    self._ingestion_complete = False
    self._processing = False

  def set_expiration_manager(self, expiration_manager):
    self._expiration_manager = expiration_manager

  def _abort_internal_only(self):
    self._wrapped_ingestion_consumer = None
    self._pending_ingestion = None

  def _abort_and_notify(self, outcome):
    self._abort_internal_only()
    self._termination_manager.abort(outcome)
    self._transmission_manager.abort(outcome)
    self._expiration_manager.abort()

  def _next(self):
    """Computes the next step for ingestion.

    Returns:
      A payload, complete, continue triplet indicating what payload (if any) is
        available to feed into customer code, whether or not the sequence of
        payloads has terminated, and whether or not there is anything
        immediately actionable to call customer code to do.
    """
    if self._pending_ingestion is None:
      return None, False, False
    elif self._pending_ingestion:
      payload = self._pending_ingestion.pop(0)
      complete = self._ingestion_complete and not self._pending_ingestion
      return payload, complete, True
    elif self._ingestion_complete:
      return None, True, True
    else:
      return None, False, False

  def _process(self, wrapped_ingestion_consumer, payload, complete):
    """A method to call to execute customer code.

    This object's lock must *not* be held when calling this method.

    Args:
      wrapped_ingestion_consumer: The _WrappedConsumer with which to pass
        payloads to customer code.
      payload: A customer payload. May be None only if complete is True.
      complete: Whether or not the sequence of payloads to pass to the customer
        has concluded.
    """
    while True:
      consumption_outcome = callable_util.call_logging_exceptions(
          wrapped_ingestion_consumer.moar, _CONSUME_EXCEPTION_LOG_MESSAGE,
          payload, complete)
      if consumption_outcome.exception is None:
        if consumption_outcome.return_value:
          with self._lock:
            if complete:
              self._pending_ingestion = None
              self._termination_manager.ingestion_complete()
              return
            else:
              payload, complete, moar = self._next()
              if not moar:
                self._processing = False
                return
        else:
          with self._lock:
            if self._pending_ingestion is not None:
              self._abort_and_notify(self._failure_outcome)
            self._processing = False
            return
      else:
        with self._lock:
          self._abort_and_notify(self._failure_outcome)
          self._processing = False
          return

  def start(self, requirement):
    if self._pending_ingestion is not None:
      def initialize():
        consumer_creation_outcome = callable_util.call_logging_exceptions(
            self._consumer_creator.create_consumer,
            _CREATE_CONSUMER_EXCEPTION_LOG_MESSAGE, requirement)
        if consumer_creation_outcome.return_value is None:
          with self._lock:
            self._abort_and_notify(self._failure_outcome)
            self._processing = False
        elif consumer_creation_outcome.return_value.remote_error:
          with self._lock:
            self._abort_and_notify(interfaces.Outcome.RECEPTION_FAILURE)
            self._processing = False
        elif consumer_creation_outcome.return_value.abandoned:
          with self._lock:
            if self._pending_ingestion is not None:
              self._abort_and_notify(self._failure_outcome)
            self._processing = False
        else:
          wrapped_ingestion_consumer = _WrappedConsumer(
              consumer_creation_outcome.return_value.consumer)
          with self._lock:
            self._wrapped_ingestion_consumer = wrapped_ingestion_consumer
            payload, complete, moar = self._next()
            if not moar:
              self._processing = False
              return

          self._process(wrapped_ingestion_consumer, payload, complete)

      self._pool.submit(
          callable_util.with_exceptions_logged(
              initialize, _constants.INTERNAL_ERROR_LOG_MESSAGE))
      self._processing = True

  def consume(self, payload):
    if self._ingestion_complete:
      self._abort_and_notify(self._failure_outcome)
    elif self._pending_ingestion is not None:
      if self._processing:
        self._pending_ingestion.append(payload)
      else:
        self._pool.submit(
            callable_util.with_exceptions_logged(
                self._process, _constants.INTERNAL_ERROR_LOG_MESSAGE),
            self._wrapped_ingestion_consumer, payload, False)
        self._processing = True

  def terminate(self):
    if self._ingestion_complete:
      self._abort_and_notify(self._failure_outcome)
    else:
      self._ingestion_complete = True
      if self._pending_ingestion is not None and not self._processing:
        self._pool.submit(
            callable_util.with_exceptions_logged(
                self._process, _constants.INTERNAL_ERROR_LOG_MESSAGE),
            self._wrapped_ingestion_consumer, None, True)
        self._processing = True

  def consume_and_terminate(self, payload):
    if self._ingestion_complete:
      self._abort_and_notify(self._failure_outcome)
    else:
      self._ingestion_complete = True
      if self._pending_ingestion is not None:
        if self._processing:
          self._pending_ingestion.append(payload)
        else:
          self._pool.submit(
              callable_util.with_exceptions_logged(
                  self._process, _constants.INTERNAL_ERROR_LOG_MESSAGE),
              self._wrapped_ingestion_consumer, payload, True)
          self._processing = True

  def abort(self):
    """See _interfaces.IngestionManager.abort for specification."""
    self._abort_internal_only()


def front_ingestion_manager(
    lock, pool, subscription, termination_manager, transmission_manager,
    operation_context):
  """Creates an IngestionManager appropriate for front-side use.

  Args:
    lock: The operation-wide lock.
    pool: A thread pool in which to execute customer code.
    subscription: A interfaces.ServicedSubscription indicating the
      customer's interest in the results of the operation.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the
      operation.
    operation_context: A interfaces.OperationContext for the operation.

  Returns:
    An IngestionManager appropriate for front-side use.
  """
  ingestion_manager = _IngestionManager(
      lock, pool, _FrontConsumerCreator(subscription, operation_context),
      interfaces.Outcome.SERVICED_FAILURE, termination_manager,
      transmission_manager)
  ingestion_manager.start(None)
  return ingestion_manager


def back_ingestion_manager(
    lock, pool, servicer, termination_manager, transmission_manager,
    operation_context, emission_consumer):
  """Creates an IngestionManager appropriate for back-side use.

  Args:
    lock: The operation-wide lock.
    pool: A thread pool in which to execute customer code.
    servicer: A interfaces.Servicer for servicing the operation.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the
      operation.
    operation_context: A interfaces.OperationContext for the operation.
    emission_consumer: The _interfaces.EmissionConsumer for the operation.

  Returns:
    An IngestionManager appropriate for back-side use.
  """
  ingestion_manager = _IngestionManager(
      lock, pool, _BackConsumerCreator(
          servicer, operation_context, emission_consumer),
      interfaces.Outcome.SERVICER_FAILURE, termination_manager,
      transmission_manager)
  return ingestion_manager
