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

"""In-memory implementations of base layer interfaces."""

import threading

from grpc.framework.base import _constants
from grpc.framework.base import interfaces
from grpc.framework.foundation import callable_util


class _Serializer(object):
  """A utility for serializing values that may arrive concurrently."""

  def __init__(self, pool):
    self._lock = threading.Lock()
    self._pool = pool
    self._sink = None
    self._spinning = False
    self._values = []

  def _spin(self, sink, value):
    while True:
      sink(value)
      with self._lock:
        if self._sink is None or not self._values:
          self._spinning = False
          return
        else:
          sink, value = self._sink, self._values.pop(0)

  def set_sink(self, sink):
    with self._lock:
      self._sink = sink
      if sink is not None and self._values and not self._spinning:
        self._spinning = True
        self._pool.submit(
            callable_util.with_exceptions_logged(
                self._spin, _constants.INTERNAL_ERROR_LOG_MESSAGE),
            sink, self._values.pop(0))

  def add_value(self, value):
    with self._lock:
      if self._sink and not self._spinning:
        self._spinning = True
        self._pool.submit(
            callable_util.with_exceptions_logged(
                self._spin, _constants.INTERNAL_ERROR_LOG_MESSAGE),
            self._sink, value)
      else:
        self._values.append(value)


class Link(interfaces.ForeLink, interfaces.RearLink):
  """A trivial implementation of interfaces.ForeLink and interfaces.RearLink."""

  def __init__(self, pool):
    """Constructor.

    Args:
      pool: A thread pool to be used for serializing ticket exchange in each
        direction.
    """
    self._front_to_back = _Serializer(pool)
    self._back_to_front = _Serializer(pool)

  def join_fore_link(self, fore_link):
    """See interfaces.RearLink.join_fore_link for specification."""
    self._back_to_front.set_sink(fore_link.accept_back_to_front_ticket)

  def join_rear_link(self, rear_link):
    """See interfaces.ForeLink.join_rear_link for specification."""
    self._front_to_back.set_sink(rear_link.accept_front_to_back_ticket)

  def accept_front_to_back_ticket(self, ticket):
    """See interfaces.ForeLink.accept_front_to_back_ticket for specification."""
    self._front_to_back.add_value(ticket)

  def accept_back_to_front_ticket(self, ticket):
    """See interfaces.RearLink.accept_back_to_front_ticket for specification."""
    self._back_to_front.add_value(ticket)
