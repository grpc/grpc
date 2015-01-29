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

"""Affords a Future implementation based on Python's threading.Timer."""

import threading
import time

from _framework.foundation import future


class TimerFuture(future.Future):
  """A Future implementation based around Timer objects."""

  def __init__(self, compute_time, computation):
    """Constructor.

    Args:
      compute_time: The time after which to begin this future's computation.
      computation: The computation to be performed within this Future.
    """
    self._lock = threading.Lock()
    self._compute_time = compute_time
    self._computation = computation
    self._timer = None
    self._computing = False
    self._computed = False
    self._cancelled = False
    self._outcome = None
    self._waiting = []

  def _compute(self):
    """Performs the computation embedded in this Future.

    Or doesn't, if the time to perform it has not yet arrived.
    """
    with self._lock:
      time_remaining = self._compute_time - time.time()
      if 0 < time_remaining:
        self._timer = threading.Timer(time_remaining, self._compute)
        self._timer.start()
        return
      else:
        self._computing = True

    try:
      returned_value = self._computation()
      outcome = future.returned(returned_value)
    except Exception as e:  # pylint: disable=broad-except
      outcome = future.raised(e)

    with self._lock:
      self._computing = False
      self._computed = True
      self._outcome = outcome
      waiting = self._waiting

    for callback in waiting:
      callback(outcome)

  def start(self):
    """Starts this Future.

    This must be called exactly once, immediately after construction.
    """
    with self._lock:
      self._timer = threading.Timer(
          self._compute_time - time.time(), self._compute)
      self._timer.start()

  def cancel(self):
    """See future.Future.cancel for specification."""
    with self._lock:
      if self._computing or self._computed:
        return False
      elif self._cancelled:
        return True
      else:
        self._timer.cancel()
        self._cancelled = True
        self._outcome = future.aborted()
        outcome = self._outcome
        waiting = self._waiting

    for callback in waiting:
      try:
        callback(outcome)
      except Exception:  # pylint: disable=broad-except
        pass

    return True

  def cancelled(self):
    """See future.Future.cancelled for specification."""
    with self._lock:
      return self._cancelled

  def done(self):
    """See future.Future.done for specification."""
    with self._lock:
      return self._computed

  def outcome(self):
    """See future.Future.outcome for specification."""
    with self._lock:
      if self._computed or self._cancelled:
        return self._outcome

      condition = threading.Condition()
      def notify_condition(unused_outcome):
        with condition:
          condition.notify()
      self._waiting.append(notify_condition)

    with condition:
      condition.wait()

    with self._lock:
      return self._outcome

  def add_done_callback(self, callback):
    """See future.Future.add_done_callback for specification."""
    with self._lock:
      if not self._computed and not self._cancelled:
        self._waiting.append(callback)
        return
      else:
        outcome = self._outcome

    callback(outcome)
