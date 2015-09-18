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

"""State and behavior for operation expiration."""

import time

from grpc.framework.base import _interfaces
from grpc.framework.base import interfaces
from grpc.framework.foundation import later


class _ExpirationManager(_interfaces.ExpirationManager):
  """An implementation of _interfaces.ExpirationManager."""

  def __init__(
      self, lock, termination_manager, transmission_manager, ingestion_manager,
      commencement, timeout, maximum_timeout):
    """Constructor.

    Args:
      lock: The operation-wide lock.
      termination_manager: The _interfaces.TerminationManager for the operation.
      transmission_manager: The _interfaces.TransmissionManager for the
        operation.
      ingestion_manager: The _interfaces.IngestionManager for the operation.
      commencement: The time in seconds since the epoch at which the operation
        began.
      timeout: A length of time in seconds to allow for the operation to run.
      maximum_timeout: The maximum length of time in seconds to allow for the
        operation to run despite what is requested via this object's
        change_timout method.
    """
    self._lock = lock
    self._termination_manager = termination_manager
    self._transmission_manager = transmission_manager
    self._ingestion_manager = ingestion_manager
    self._commencement = commencement
    self._maximum_timeout = maximum_timeout

    self._timeout = timeout
    self._deadline = commencement + timeout
    self._index = None
    self._future = None

  def _expire(self, index):
    with self._lock:
      if self._future is not None and index == self._index:
        self._future = None
        self._termination_manager.abort(interfaces.Outcome.EXPIRED)
        self._transmission_manager.abort(interfaces.Outcome.EXPIRED)
        self._ingestion_manager.abort()

  def start(self):
    self._index = 0
    self._future = later.later(self._timeout, lambda: self._expire(0))

  def change_timeout(self, timeout):
    if self._future is not None and timeout != self._timeout:
      self._future.cancel()
      new_timeout = min(timeout, self._maximum_timeout)
      new_index = self._index + 1
      self._timeout = new_timeout
      self._deadline = self._commencement + new_timeout
      self._index = new_index
      delay = self._deadline - time.time()
      self._future = later.later(
          delay, lambda: self._expire(new_index))

  def deadline(self):
    return self._deadline

  def abort(self):
    if self._future:
      self._future.cancel()
      self._future = None
    self._deadline_index = None


def front_expiration_manager(
    lock, termination_manager, transmission_manager, ingestion_manager,
    timeout):
  """Creates an _interfaces.ExpirationManager appropriate for front-side use.

  Args:
    lock: The operation-wide lock.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the
      operation.
    ingestion_manager: The _interfaces.IngestionManager for the operation.
    timeout: A length of time in seconds to allow for the operation to run.

  Returns:
    An _interfaces.ExpirationManager appropriate for front-side use.
  """
  commencement = time.time()
  expiration_manager = _ExpirationManager(
      lock, termination_manager, transmission_manager, ingestion_manager,
      commencement, timeout, timeout)
  expiration_manager.start()
  return expiration_manager


def back_expiration_manager(
    lock, termination_manager, transmission_manager, ingestion_manager,
    timeout, default_timeout, maximum_timeout):
  """Creates an _interfaces.ExpirationManager appropriate for back-side use.

  Args:
    lock: The operation-wide lock.
    termination_manager: The _interfaces.TerminationManager for the operation.
    transmission_manager: The _interfaces.TransmissionManager for the
      operation.
    ingestion_manager: The _interfaces.IngestionManager for the operation.
    timeout: A length of time in seconds to allow for the operation to run. May
      be None in which case default_timeout will be used.
    default_timeout: The default length of time in seconds to allow for the
      operation to run if the front-side customer has not specified such a value
      (or if the value they specified is not yet known).
    maximum_timeout: The maximum length of time in seconds to allow for the
      operation to run.

  Returns:
    An _interfaces.ExpirationManager appropriate for back-side use.
  """
  commencement = time.time()
  expiration_manager = _ExpirationManager(
      lock, termination_manager, transmission_manager, ingestion_manager,
      commencement, default_timeout if timeout is None else timeout,
      maximum_timeout)
  expiration_manager.start()
  return expiration_manager
