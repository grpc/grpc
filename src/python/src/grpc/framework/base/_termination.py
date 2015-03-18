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

"""State and behavior for operation termination."""

import enum

from grpc.framework.base import _constants
from grpc.framework.base import _interfaces
from grpc.framework.base import interfaces
from grpc.framework.foundation import callable_util

_CALLBACK_EXCEPTION_LOG_MESSAGE = 'Exception calling termination callback!'


@enum.unique
class _Requirement(enum.Enum):
  """Symbols indicating events required for termination."""

  EMISSION = 'emission'
  TRANSMISSION = 'transmission'
  INGESTION = 'ingestion'

_FRONT_NOT_LISTENING_REQUIREMENTS = (_Requirement.TRANSMISSION,)
_BACK_NOT_LISTENING_REQUIREMENTS = (
    _Requirement.EMISSION, _Requirement.INGESTION,)
_LISTENING_REQUIREMENTS = (
    _Requirement.TRANSMISSION, _Requirement.INGESTION,)


class _TerminationManager(_interfaces.TerminationManager):
  """An implementation of _interfaces.TerminationManager."""

  def __init__(
      self, work_pool, utility_pool, action, requirements, local_failure):
    """Constructor.

    Args:
      work_pool: A thread pool in which customer work will be done.
      utility_pool: A thread pool in which work utility work will be done.
      action: An action to call on operation termination.
      requirements: A combination of _Requirement values identifying what
        must finish for the operation to be considered completed.
      local_failure: An interfaces.Outcome specifying what constitutes local
        failure of customer work.
    """
    self._work_pool = work_pool
    self._utility_pool = utility_pool
    self._action = action
    self._local_failure = local_failure
    self._has_locally_failed = False
    self._expiration_manager = None

    self._outstanding_requirements = set(requirements)
    self._outcome = None
    self._callbacks = []

  def set_expiration_manager(self, expiration_manager):
    self._expiration_manager = expiration_manager

  def _terminate(self, outcome):
    """Terminates the operation.

    Args:
      outcome: An interfaces.Outcome describing the outcome of the operation.
    """
    self._expiration_manager.abort()
    self._outstanding_requirements = None
    callbacks = list(self._callbacks)
    self._callbacks = None
    self._outcome = outcome

    act = callable_util.with_exceptions_logged(
        self._action, _constants.INTERNAL_ERROR_LOG_MESSAGE)

    if self._has_locally_failed:
      self._utility_pool.submit(act, outcome)
    else:
      def call_callbacks_and_act(callbacks, outcome):
        for callback in callbacks:
          callback_outcome = callable_util.call_logging_exceptions(
              callback, _CALLBACK_EXCEPTION_LOG_MESSAGE, outcome)
          if callback_outcome.exception is not None:
            outcome = self._local_failure
            break
        self._utility_pool.submit(act, outcome)

      self._work_pool.submit(callable_util.with_exceptions_logged(
          call_callbacks_and_act,
          _constants.INTERNAL_ERROR_LOG_MESSAGE),
                             callbacks, outcome)

  def is_active(self):
    """See _interfaces.TerminationManager.is_active for specification."""
    return self._outstanding_requirements is not None

  def add_callback(self, callback):
    """See _interfaces.TerminationManager.add_callback for specification."""
    if not self._has_locally_failed:
      if self._outstanding_requirements is None:
        self._work_pool.submit(
            callable_util.with_exceptions_logged(
                callback, _CALLBACK_EXCEPTION_LOG_MESSAGE), self._outcome)
      else:
        self._callbacks.append(callback)

  def emission_complete(self):
    """See superclass method for specification."""
    if self._outstanding_requirements is not None:
      self._outstanding_requirements.discard(_Requirement.EMISSION)
      if not self._outstanding_requirements:
        self._terminate(interfaces.Outcome.COMPLETED)

  def transmission_complete(self):
    """See superclass method for specification."""
    if self._outstanding_requirements is not None:
      self._outstanding_requirements.discard(_Requirement.TRANSMISSION)
      if not self._outstanding_requirements:
        self._terminate(interfaces.Outcome.COMPLETED)

  def ingestion_complete(self):
    """See superclass method for specification."""
    if self._outstanding_requirements is not None:
      self._outstanding_requirements.discard(_Requirement.INGESTION)
      if not self._outstanding_requirements:
        self._terminate(interfaces.Outcome.COMPLETED)

  def abort(self, outcome):
    """See _interfaces.TerminationManager.abort for specification."""
    if outcome is self._local_failure:
      self._has_failed_locally = True
    if self._outstanding_requirements is not None:
      self._terminate(outcome)


def front_termination_manager(
    work_pool, utility_pool, action, subscription_kind):
  """Creates a TerminationManager appropriate for front-side use.

  Args:
    work_pool: A thread pool in which customer work will be done.
    utility_pool: A thread pool in which work utility work will be done.
    action: An action to call on operation termination.
    subscription_kind: An interfaces.ServicedSubscription.Kind value.

  Returns:
    A TerminationManager appropriate for front-side use.
  """
  if subscription_kind is interfaces.ServicedSubscription.Kind.NONE:
    requirements = _FRONT_NOT_LISTENING_REQUIREMENTS
  else:
    requirements = _LISTENING_REQUIREMENTS

  return _TerminationManager(
      work_pool, utility_pool, action, requirements,
      interfaces.Outcome.SERVICED_FAILURE)


def back_termination_manager(work_pool, utility_pool, action, subscription_kind):
  """Creates a TerminationManager appropriate for back-side use.

  Args:
    work_pool: A thread pool in which customer work will be done.
    utility_pool: A thread pool in which work utility work will be done.
    action: An action to call on operation termination.
    subscription_kind: An interfaces.ServicedSubscription.Kind value.

  Returns:
    A TerminationManager appropriate for back-side use.
  """
  if subscription_kind is interfaces.ServicedSubscription.Kind.NONE:
    requirements = _BACK_NOT_LISTENING_REQUIREMENTS
  else:
    requirements = _LISTENING_REQUIREMENTS

  return _TerminationManager(
      work_pool, utility_pool, action, requirements,
      interfaces.Outcome.SERVICER_FAILURE)
