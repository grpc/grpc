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

"""State and behavior for operation cancellation."""

from grpc.framework.base import _interfaces
from grpc.framework.base import interfaces


class CancellationManager(_interfaces.CancellationManager):
  """An implementation of _interfaces.CancellationManager."""

  def __init__(
      self, lock, termination_manager, transmission_manager, ingestion_manager,
      expiration_manager):
    """Constructor.

    Args:
      lock: The operation-wide lock.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
      ingestion_manager: The _interfaces.IngestionManager for the operation.
      expiration_manager: The _interfaces.ExpirationManager for the operation.
    """
    self._lock = lock
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._ingestion_manager = ingestion_manager
    self._expiration_manager = expiration_manager

  def cancel(self):
    """See _interfaces.CancellationManager.cancel for specification."""
    with self._lock:
      self._termination_manager.abort(interfaces.Outcome.CANCELLED)
      self._transmission_manager.abort(interfaces.Outcome.CANCELLED)
      self._ingestion_manager.abort()
      self._expiration_manager.abort()
