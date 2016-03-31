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

"""Utilities for creating Base-layer objects for use in Face-layer tests."""

import abc

import six

# interfaces is referenced from specification in this module.
from grpc.framework.base import util as _base_util
from grpc.framework.base import implementations
from grpc.framework.base import in_memory
from grpc.framework.base import interfaces  # pylint: disable=unused-import
from grpc.framework.foundation import logging_pool

_POOL_SIZE_LIMIT = 5

_MAXIMUM_TIMEOUT = 90


class LinkedPair(six.with_metaclass(abc.ABCMeta)):
  """A Front and Back that are linked to one another.

  Attributes:
    front: An interfaces.Front.
    back: An interfaces.Back.
  """

  @abc.abstractmethod
  def shut_down(self):
    """Shuts down this object and releases its resources."""
    raise NotImplementedError()


class _LinkedPair(LinkedPair):

  def __init__(self, front, back, pools):
    self.front = front
    self.back = back
    self._pools = pools

  def shut_down(self):
    _base_util.wait_for_idle(self.front)
    _base_util.wait_for_idle(self.back)

    for pool in self._pools:
      pool.shutdown(wait=True)


def linked_pair(servicer, default_timeout):
  """Creates a Server and Stub linked together for use."""
  link_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  front_work_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  front_transmission_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  front_utility_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  back_work_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  back_transmission_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  back_utility_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  pools = (
      link_pool,
      front_work_pool, front_transmission_pool, front_utility_pool,
      back_work_pool, back_transmission_pool, back_utility_pool)

  link = in_memory.Link(link_pool)
  front = implementations.front_link(
      front_work_pool, front_transmission_pool, front_utility_pool)
  back = implementations.back_link(
      servicer, back_work_pool, back_transmission_pool, back_utility_pool,
      default_timeout, _MAXIMUM_TIMEOUT)
  front.join_rear_link(link)
  link.join_fore_link(front)
  back.join_fore_link(link)
  link.join_rear_link(back)

  return _LinkedPair(front, back, pools)
