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

"""State and behavior for passing protocol objects in an operation."""

import collections
import enum

from grpc.framework.core import _constants
from grpc.framework.core import _interfaces
from grpc.framework.core import _utilities
from grpc.framework.foundation import callable_util
from grpc.framework.interfaces.base import base

_EXCEPTION_LOG_MESSAGE = 'Exception delivering protocol object!'

_LOCAL_FAILURE_OUTCOME = _utilities.Outcome(
    base.Outcome.Kind.LOCAL_FAILURE, None, None)


class _Awaited(
    collections.namedtuple('_Awaited', ('kind', 'value',))):

  @enum.unique
  class Kind(enum.Enum):
    NOT_YET_ARRIVED = 'not yet arrived'
    ARRIVED = 'arrived'

_NOT_YET_ARRIVED = _Awaited(_Awaited.Kind.NOT_YET_ARRIVED, None)
_ARRIVED_AND_NONE = _Awaited(_Awaited.Kind.ARRIVED, None)


class _Transitory(
    collections.namedtuple('_Transitory', ('kind', 'value',))):

  @enum.unique
  class Kind(enum.Enum):
    NOT_YET_SEEN = 'not yet seen'
    PRESENT = 'present'
    GONE = 'gone'

_NOT_YET_SEEN = _Transitory(_Transitory.Kind.NOT_YET_SEEN, None)
_GONE = _Transitory(_Transitory.Kind.GONE, None)


class _ProtocolManager(_interfaces.ProtocolManager):
  """An implementation of _interfaces.ExpirationManager."""

  def __init__(
      self, protocol_receiver, lock, pool, termination_manager,
      transmission_manager, expiration_manager):
    """Constructor.

    Args:
      protocol_receiver: An _Awaited wrapping of the base.ProtocolReceiver to
        which protocol objects should be passed during the operation. May be
        of kind _Awaited.Kind.NOT_YET_ARRIVED if the customer's subscription is
        not yet known and may be of kind _Awaited.Kind.ARRIVED but with a value
        of None if the customer's subscription did not include a
        ProtocolReceiver.
      lock: The operation-wide lock.
      pool: A thread pool.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
      expiration_manager: The _interfaces.ExpirationManager for the operation.
    """
    self._lock = lock
    self._pool = pool
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._expiration_manager = expiration_manager

    self._protocol_receiver = protocol_receiver
    self._context = _NOT_YET_SEEN

  def _abort_and_notify(self, outcome):
    if self._termination_manager.outcome is None:
      self._termination_manager.abort(outcome)
      self._transmission_manager.abort(outcome)
      self._expiration_manager.terminate()

  def _deliver(self, behavior, value):
    def deliver():
      delivery_outcome = callable_util.call_logging_exceptions(
          behavior, _EXCEPTION_LOG_MESSAGE, value)
      if delivery_outcome.kind is callable_util.Outcome.Kind.RAISED:
        with self._lock:
          self._abort_and_notify(_LOCAL_FAILURE_OUTCOME)
    self._pool.submit(
        callable_util.with_exceptions_logged(
            deliver, _constants.INTERNAL_ERROR_LOG_MESSAGE))

  def set_protocol_receiver(self, protocol_receiver):
    """See _interfaces.ProtocolManager.set_protocol_receiver for spec."""
    self._protocol_receiver = _Awaited(_Awaited.Kind.ARRIVED, protocol_receiver)
    if (self._context.kind is _Transitory.Kind.PRESENT and
        protocol_receiver is not None):
      self._deliver(protocol_receiver.context, self._context.value)
      self._context = _GONE

  def accept_protocol_context(self, protocol_context):
    """See _interfaces.ProtocolManager.accept_protocol_context for spec."""
    if self._protocol_receiver.kind is _Awaited.Kind.ARRIVED:
      if self._protocol_receiver.value is not None:
        self._deliver(self._protocol_receiver.value.context, protocol_context)
      self._context = _GONE
    else:
      self._context = _Transitory(_Transitory.Kind.PRESENT, protocol_context)


def invocation_protocol_manager(
    subscription, lock, pool, termination_manager, transmission_manager,
    expiration_manager):
  """Creates an _interfaces.ProtocolManager for invocation-side use.

  Args:
    subscription: The local customer's subscription to the operation.
    lock: The operation-wide lock.
    pool: A thread pool.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the
      operation.
    expiration_manager: The _interfaces.ExpirationManager for the operation.
  """
  if subscription.kind is base.Subscription.Kind.FULL:
    awaited_protocol_receiver = _Awaited(
        _Awaited.Kind.ARRIVED, subscription.protocol_receiver)
  else:
    awaited_protocol_receiver = _ARRIVED_AND_NONE
  return _ProtocolManager(
      awaited_protocol_receiver, lock, pool, termination_manager,
      transmission_manager, expiration_manager)


def service_protocol_manager(
    lock, pool, termination_manager, transmission_manager, expiration_manager):
  """Creates an _interfaces.ProtocolManager for service-side use.

  Args:
    lock: The operation-wide lock.
    pool: A thread pool.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the
      operation.
    expiration_manager: The _interfaces.ExpirationManager for the operation.
  """
  return _ProtocolManager(
      _NOT_YET_ARRIVED, lock, pool, termination_manager, transmission_manager,
      expiration_manager)
