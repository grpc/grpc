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

"""Implementations of in-order work deference."""

import abc
import enum
import threading

from grpc.framework.foundation import activated
from grpc.framework.foundation import logging_pool

_NULL_BEHAVIOR = lambda unused_value: None


class Relay(object):
  """Performs work submitted to it in another thread.

  Performs work in the order in which work was submitted to it; otherwise there
  would be no reason to use an implementation of this interface instead of a
  thread pool.
  """

  @abc.abstractmethod
  def add_value(self, value):
    """Adds a value to be passed to the behavior registered with this Relay.

    Args:
      value: A value that will be passed to a call made in another thread to the
        behavior registered with this Relay.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def set_behavior(self, behavior):
    """Sets the behavior that this Relay should call when passed values.

    Args:
      behavior: The behavior that this Relay should call in another thread when
        passed a value, or None to have passed values ignored.
    """
    raise NotImplementedError()


class _PoolRelay(activated.Activated, Relay):

  @enum.unique
  class _State(enum.Enum):
    INACTIVE = 'inactive'
    IDLE = 'idle'
    SPINNING = 'spinning'

  def __init__(self, pool, behavior):
    self._condition = threading.Condition()
    self._pool = pool
    self._own_pool = pool is None
    self._state = _PoolRelay._State.INACTIVE
    self._activated = False
    self._spinning = False
    self._values = []
    self._behavior = _NULL_BEHAVIOR if behavior is None else behavior

  def _spin(self, behavior, value):
    while True:
      behavior(value)
      with self._condition:
        if self._values:
          value = self._values.pop(0)
          behavior = self._behavior
        else:
          self._state = _PoolRelay._State.IDLE
          self._condition.notify_all()
          break

  def add_value(self, value):
    with self._condition:
      if self._state is _PoolRelay._State.INACTIVE:
        raise ValueError('add_value not valid on inactive Relay!')
      elif self._state is _PoolRelay._State.IDLE:
        self._pool.submit(self._spin, self._behavior, value)
        self._state = _PoolRelay._State.SPINNING
      else:
        self._values.append(value)

  def set_behavior(self, behavior):
    with self._condition:
      self._behavior = _NULL_BEHAVIOR if behavior is None else behavior

  def _start(self):
    with self._condition:
      self._state = _PoolRelay._State.IDLE
      if self._own_pool:
        self._pool = logging_pool.pool(1)
      return self

  def _stop(self):
    with self._condition:
      while self._state is _PoolRelay._State.SPINNING:
        self._condition.wait()
      if self._own_pool:
        self._pool.shutdown(wait=True)
      self._state = _PoolRelay._State.INACTIVE

  def __enter__(self):
    return self._start()

  def __exit__(self, exc_type, exc_val, exc_tb):
    self._stop()
    return False

  def start(self):
    return self._start()

  def stop(self):
    self._stop()


def relay(behavior):
  """Creates a Relay.

  Args:
    behavior: The behavior to be called by the created Relay, or None to have
      passed values dropped until a different behavior is given to the returned
      Relay later.

  Returns:
    An object that is both an activated.Activated and a Relay. The object is
      only valid for use as a Relay when activated.
  """
  return _PoolRelay(None, behavior)


def pool_relay(pool, behavior):
  """Creates a Relay that uses a given thread pool.

  This object will make use of at most one thread in the given pool.

  Args:
    pool: A futures.ThreadPoolExecutor for use by the created Relay.
    behavior: The behavior to be called by the created Relay, or None to have
      passed values dropped until a different behavior is given to the returned
      Relay later.

  Returns:
    An object that is both an activated.Activated and a Relay. The object is
      only valid for use as a Relay when activated.
  """
  return _PoolRelay(pool, behavior)
