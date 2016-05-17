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

from grpc.framework.core import _interfaces
from grpc.framework.core import _utilities
from grpc.framework.interfaces.base import base


class EmissionManager(_interfaces.EmissionManager):
  """An EmissionManager implementation."""

  def __init__(
      self, lock, termination_manager, transmission_manager,
      expiration_manager):
    """Constructor.

    Args:
      lock: The operation-wide lock.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
      expiration_manager: The _interfaces.ExpirationManager for the operation.
    """
    self._lock = lock
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._expiration_manager = expiration_manager
    self._ingestion_manager = None

    self._initial_metadata_seen = False
    self._payload_seen = False
    self._completion_seen = False

  def set_ingestion_manager(self, ingestion_manager):
    """Sets the ingestion manager with which this manager will cooperate.

    Args:
      ingestion_manager: The _interfaces.IngestionManager for the operation.
    """
    self._ingestion_manager = ingestion_manager

  def advance(
      self, initial_metadata=None, payload=None, completion=None,
      allowance=None):
    initial_metadata_present = initial_metadata is not None
    payload_present = payload is not None
    completion_present = completion is not None
    allowance_present = allowance is not None
    with self._lock:
      if self._termination_manager.outcome is None:
        if (initial_metadata_present and (
                self._initial_metadata_seen or self._payload_seen or
                self._completion_seen) or
            payload_present and self._completion_seen or
            completion_present and self._completion_seen or
            allowance_present and allowance <= 0):
          outcome = _utilities.Outcome(
              base.Outcome.Kind.LOCAL_FAILURE, None, None)
          self._termination_manager.abort(outcome)
          self._transmission_manager.abort(outcome)
          self._expiration_manager.terminate()
        else:
          self._initial_metadata_seen |= initial_metadata_present
          self._payload_seen |= payload_present
          self._completion_seen |= completion_present
          if completion_present:
            self._termination_manager.emission_complete()
            self._ingestion_manager.local_emissions_done()
          self._transmission_manager.advance(
              initial_metadata, payload, completion, allowance)
          if allowance_present:
            self._ingestion_manager.add_local_allowance(allowance)
