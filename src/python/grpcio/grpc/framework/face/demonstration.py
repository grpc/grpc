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

"""Demonstration-suitable implementation of the face layer of RPC Framework."""

from grpc.framework.base import util as _base_util
from grpc.framework.base import implementations as _base_implementations
from grpc.framework.face import implementations
from grpc.framework.foundation import logging_pool

_POOL_SIZE_LIMIT = 5

_MAXIMUM_TIMEOUT = 90


class LinkedPair(object):
  """A Server and Stub that are linked to one another.

  Attributes:
    server: A Server.
    stub: A Stub.
  """

  def shut_down(self):
    """Shuts down this object and releases its resources."""
    raise NotImplementedError()


class _LinkedPair(LinkedPair):

  def __init__(self, server, stub, front, back, pools):
    self.server = server
    self.stub = stub
    self._front = front
    self._back = back
    self._pools = pools

  def shut_down(self):
    _base_util.wait_for_idle(self._front)
    _base_util.wait_for_idle(self._back)

    for pool in self._pools:
      pool.shutdown(wait=True)


def server_and_stub(
    default_timeout,
    inline_value_in_value_out_methods=None,
    inline_value_in_stream_out_methods=None,
    inline_stream_in_value_out_methods=None,
    inline_stream_in_stream_out_methods=None,
    event_value_in_value_out_methods=None,
    event_value_in_stream_out_methods=None,
    event_stream_in_value_out_methods=None,
    event_stream_in_stream_out_methods=None,
    multi_method=None):
  """Creates a Server and Stub linked together for use."""
  front_work_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  front_transmission_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  front_utility_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  back_work_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  back_transmission_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  back_utility_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  stub_pool = logging_pool.pool(_POOL_SIZE_LIMIT)
  pools = (
      front_work_pool, front_transmission_pool, front_utility_pool,
      back_work_pool, back_transmission_pool, back_utility_pool,
      stub_pool)

  servicer = implementations.servicer(
      back_work_pool,
      inline_value_in_value_out_methods=inline_value_in_value_out_methods,
      inline_value_in_stream_out_methods=inline_value_in_stream_out_methods,
      inline_stream_in_value_out_methods=inline_stream_in_value_out_methods,
      inline_stream_in_stream_out_methods=inline_stream_in_stream_out_methods,
      event_value_in_value_out_methods=event_value_in_value_out_methods,
      event_value_in_stream_out_methods=event_value_in_stream_out_methods,
      event_stream_in_value_out_methods=event_stream_in_value_out_methods,
      event_stream_in_stream_out_methods=event_stream_in_stream_out_methods,
      multi_method=multi_method)

  front = _base_implementations.front_link(
      front_work_pool, front_transmission_pool, front_utility_pool)
  back = _base_implementations.back_link(
      servicer, back_work_pool, back_transmission_pool, back_utility_pool,
      default_timeout, _MAXIMUM_TIMEOUT)
  front.join_rear_link(back)
  back.join_fore_link(front)

  stub = implementations.stub(front, stub_pool)

  return _LinkedPair(implementations.server(), stub, front, back, pools)
