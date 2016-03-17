# Copyright 2015-2016, Google Inc.
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

import abc

from grpc.framework.core import _constants
from grpc.framework.core import _interfaces
from grpc.framework.core import _utilities
from grpc.framework.foundation import callable_util
from grpc.framework.interfaces.base import base


def _invocation_completion_predicate(
    unused_emission_complete, unused_transmission_complete,
    unused_reception_complete, ingestion_complete):
  return ingestion_complete


def _service_completion_predicate(
    unused_emission_complete, transmission_complete, unused_reception_complete,
    ingestion_complete):
  return transmission_complete and ingestion_complete


class TerminationManager(_interfaces.TerminationManager):
  """A _interfaces.TransmissionManager on which another manager may be set."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def set_expiration_manager(self, expiration_manager):
    """Sets the expiration manager with which this manager will interact.

    Args:
      expiration_manager: The _interfaces.ExpirationManager associated with the
        current operation.
    """
    raise NotImplementedError()


class _TerminationManager(TerminationManager):
  """An implementation of TerminationManager."""

  def __init__(self, predicate, action, pool):
    """Constructor.

    Args:
      predicate: One of _invocation_completion_predicate or
        _service_completion_predicate to be used to determine when the operation
        has completed.
      action: A behavior to pass the operation outcome's kind on operation
        termination.
      pool: A thread pool.
    """
    self._predicate = predicate
    self._action = action
    self._pool = pool
    self._expiration_manager = None

    self._callbacks = []

    self._code = None
    self._details = None
    self._emission_complete = False
    self._transmission_complete = False
    self._reception_complete = False
    self._ingestion_complete = False

    # The None-ness of outcome is the operation-wide record of whether and how
    # the operation has terminated.
    self.outcome = None

  def set_expiration_manager(self, expiration_manager):
    self._expiration_manager = expiration_manager

  def _terminate_internal_only(self, outcome):
    """Terminates the operation.

    Args:
      outcome: A base.Outcome describing the outcome of the operation.
    """
    self.outcome = outcome
    callbacks = list(self._callbacks)
    self._callbacks = None

    act = callable_util.with_exceptions_logged(
        self._action, _constants.INTERNAL_ERROR_LOG_MESSAGE)

    # TODO(issue 3202): Don't call the local application's callbacks if it has
    # previously shown a programming defect.
    if False and outcome.kind is base.Outcome.Kind.LOCAL_FAILURE:
      self._pool.submit(act, base.Outcome.Kind.LOCAL_FAILURE)
    else:
      def call_callbacks_and_act(callbacks, outcome):
        for callback in callbacks:
          callback_outcome = callable_util.call_logging_exceptions(
              callback, _constants.TERMINATION_CALLBACK_EXCEPTION_LOG_MESSAGE,
              outcome)
          if callback_outcome.exception is not None:
            act_outcome_kind = base.Outcome.Kind.LOCAL_FAILURE
            break
        else:
          act_outcome_kind = outcome.kind
        act(act_outcome_kind)

      self._pool.submit(
          callable_util.with_exceptions_logged(
              call_callbacks_and_act, _constants.INTERNAL_ERROR_LOG_MESSAGE),
          callbacks, outcome)

  def _terminate_and_notify(self, outcome):
    self._terminate_internal_only(outcome)
    self._expiration_manager.terminate()

  def _perhaps_complete(self):
    if self._predicate(
        self._emission_complete, self._transmission_complete,
        self._reception_complete, self._ingestion_complete):
      self._terminate_and_notify(
          _utilities.Outcome(
              base.Outcome.Kind.COMPLETED, self._code, self._details))
      return True
    else:
      return False

  def is_active(self):
    """See _interfaces.TerminationManager.is_active for specification."""
    return self.outcome is None

  def add_callback(self, callback):
    """See _interfaces.TerminationManager.add_callback for specification."""
    if self.outcome is None:
      self._callbacks.append(callback)
      return None
    else:
      return self.outcome

  def emission_complete(self):
    """See superclass method for specification."""
    if self.outcome is None:
      self._emission_complete = True
      self._perhaps_complete()

  def transmission_complete(self):
    """See superclass method for specification."""
    if self.outcome is None:
      self._transmission_complete = True
      return self._perhaps_complete()
    else:
      return False

  def reception_complete(self, code, details):
    """See superclass method for specification."""
    if self.outcome is None:
      self._reception_complete = True
      self._code = code
      self._details = details
      self._perhaps_complete()

  def ingestion_complete(self):
    """See superclass method for specification."""
    if self.outcome is None:
      self._ingestion_complete = True
      self._perhaps_complete()

  def expire(self):
    """See _interfaces.TerminationManager.expire for specification."""
    self._terminate_internal_only(
        _utilities.Outcome(base.Outcome.Kind.EXPIRED, None, None))

  def abort(self, outcome):
    """See _interfaces.TerminationManager.abort for specification."""
    self._terminate_and_notify(outcome)


def invocation_termination_manager(action, pool):
  """Creates a TerminationManager appropriate for invocation-side use.

  Args:
    action: An action to call on operation termination.
    pool: A thread pool in which to execute the passed action and any
      termination callbacks that are registered during the operation.

  Returns:
    A TerminationManager appropriate for invocation-side use.
  """
  return _TerminationManager(_invocation_completion_predicate, action, pool)


def service_termination_manager(action, pool):
  """Creates a TerminationManager appropriate for service-side use.

  Args:
    action: An action to call on operation termination.
    pool: A thread pool in which to execute the passed action and any
      termination callbacks that are registered during the operation.

  Returns:
    A TerminationManager appropriate for service-side use.
  """
  return _TerminationManager(_service_completion_predicate, action, pool)
