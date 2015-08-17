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

"""State and behavior for handling emitted values."""

from grpc.framework.base import interfaces
from grpc.framework.base import _interfaces


class _EmissionManager(_interfaces.EmissionManager):
  """An implementation of _interfaces.EmissionManager."""

  def __init__(
      self, lock, failure_outcome, termination_manager, transmission_manager):
    """Constructor.

    Args:
      lock: The operation-wide lock.
      failure_outcome: Whichever one of interfaces.Outcome.SERVICED_FAILURE or
        interfaces.Outcome.SERVICER_FAILURE describes this object's methods
        being called inappropriately by customer code.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
    """
    self._lock = lock
    self._failure_outcome = failure_outcome
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._ingestion_manager = None
    self._expiration_manager = None

    self._emission_complete = False

  def set_ingestion_manager_and_expiration_manager(
      self, ingestion_manager, expiration_manager):
    self._ingestion_manager = ingestion_manager
    self._expiration_manager = expiration_manager

  def _abort(self):
    self._termination_manager.abort(self._failure_outcome)
    self._transmission_manager.abort(self._failure_outcome)
    self._ingestion_manager.abort()
    self._expiration_manager.abort()

  def consume(self, value):
    with self._lock:
      if self._emission_complete:
        self._abort()
      else:
        self._transmission_manager.inmit(value, False)

  def terminate(self):
    with self._lock:
      if not self._emission_complete:
        self._termination_manager.emission_complete()
        self._transmission_manager.inmit(None, True)
        self._emission_complete = True

  def consume_and_terminate(self, value):
    with self._lock:
      if self._emission_complete:
        self._abort()
      else:
        self._termination_manager.emission_complete()
        self._transmission_manager.inmit(value, True)
        self._emission_complete = True


def front_emission_manager(lock, termination_manager, transmission_manager):
  """Creates an _interfaces.EmissionManager appropriate for front-side use.

  Args:
    lock: The operation-wide lock.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the operation.

  Returns:
    An _interfaces.EmissionManager appropriate for front-side use.
  """
  return _EmissionManager(
      lock, interfaces.Outcome.SERVICED_FAILURE, termination_manager,
      transmission_manager)


def back_emission_manager(lock, termination_manager, transmission_manager):
  """Creates an _interfaces.EmissionManager appropriate for back-side use.

  Args:
    lock: The operation-wide lock.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the operation.

  Returns:
    An _interfaces.EmissionManager appropriate for back-side use.
  """
  return _EmissionManager(
      lock, interfaces.Outcome.SERVICER_FAILURE, termination_manager,
      transmission_manager)
