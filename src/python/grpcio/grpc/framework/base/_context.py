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

"""State and behavior for operation context."""

import time

# _interfaces is referenced from specification in this module.
from grpc.framework.base import interfaces
from grpc.framework.base import _interfaces  # pylint: disable=unused-import


class OperationContext(interfaces.OperationContext):
  """An implementation of interfaces.OperationContext."""

  def __init__(
      self, lock, operation_id, local_failure, termination_manager,
      transmission_manager):
    """Constructor.

    Args:
      lock: The operation-wide lock.
      operation_id: An object identifying the operation.
      local_failure: Whichever one of interfaces.Outcome.SERVICED_FAILURE or
        interfaces.Outcome.SERVICER_FAILURE describes local failure of
        customer code.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
    """
    self._lock = lock
    self._local_failure = local_failure
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._ingestion_manager = None
    self._expiration_manager = None

    self.operation_id = operation_id

  def set_ingestion_and_expiration_managers(
      self, ingestion_manager, expiration_manager):
    """Sets managers with which this OperationContext cooperates.

    Args:
      ingestion_manager: The _interfaces.IngestionManager for the operation.
      expiration_manager: The _interfaces.ExpirationManager for the operation.
    """
    self._ingestion_manager = ingestion_manager
    self._expiration_manager = expiration_manager

  def is_active(self):
    """See interfaces.OperationContext.is_active for specification."""
    with self._lock:
      return self._termination_manager.is_active()

  def add_termination_callback(self, callback):
    """See interfaces.OperationContext.add_termination_callback."""
    with self._lock:
      self._termination_manager.add_callback(callback)

  def time_remaining(self):
    """See interfaces.OperationContext.time_remaining for specification."""
    with self._lock:
      deadline = self._expiration_manager.deadline()
    return max(0.0, deadline - time.time())

  def fail(self, exception):
    """See interfaces.OperationContext.fail for specification."""
    with self._lock:
      self._termination_manager.abort(self._local_failure)
      self._transmission_manager.abort(self._local_failure)
      self._ingestion_manager.abort()
      self._expiration_manager.abort()
