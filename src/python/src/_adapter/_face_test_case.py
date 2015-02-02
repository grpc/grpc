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

"""Common construction and destruction for GRPC-backed Face-layer tests."""

import unittest

from _adapter import fore
from _adapter import rear
from _framework.base import util
from _framework.base.packets import implementations as tickets_implementations
from _framework.face import implementations as face_implementations
from _framework.face.testing import coverage
from _framework.face.testing import serial
from _framework.face.testing import test_case
from _framework.foundation import logging_pool

_TIMEOUT = 3
_MAXIMUM_TIMEOUT = 90
_MAXIMUM_POOL_SIZE = 400


class FaceTestCase(test_case.FaceTestCase, coverage.BlockingCoverage):
  """Provides abstract Face-layer tests a GRPC-backed implementation."""

  def set_up_implementation(
      self,
      name,
      methods,
      inline_value_in_value_out_methods,
      inline_value_in_stream_out_methods,
      inline_stream_in_value_out_methods,
      inline_stream_in_stream_out_methods,
      event_value_in_value_out_methods,
      event_value_in_stream_out_methods,
      event_stream_in_value_out_methods,
      event_stream_in_stream_out_methods,
      multi_method):
    pool = logging_pool.pool(_MAXIMUM_POOL_SIZE)

    servicer = face_implementations.servicer(
        pool,
        inline_value_in_value_out_methods=inline_value_in_value_out_methods,
        inline_value_in_stream_out_methods=inline_value_in_stream_out_methods,
        inline_stream_in_value_out_methods=inline_stream_in_value_out_methods,
        inline_stream_in_stream_out_methods=inline_stream_in_stream_out_methods,
        event_value_in_value_out_methods=event_value_in_value_out_methods,
        event_value_in_stream_out_methods=event_value_in_stream_out_methods,
        event_stream_in_value_out_methods=event_stream_in_value_out_methods,
        event_stream_in_stream_out_methods=event_stream_in_stream_out_methods,
        multi_method=multi_method)

    serialization = serial.serialization(methods)

    fore_link = fore.ForeLink(
        pool, serialization.request_deserializers,
        serialization.response_serializers)
    port = fore_link.start()
    rear_link = rear.RearLink(
        'localhost', port, pool,
        serialization.request_serializers, serialization.response_deserializers)
    rear_link.start()
    front = tickets_implementations.front(pool, pool, pool)
    back = tickets_implementations.back(
        servicer, pool, pool, pool, _TIMEOUT, _MAXIMUM_TIMEOUT)
    fore_link.join_rear_link(back)
    back.join_fore_link(fore_link)
    rear_link.join_fore_link(front)
    front.join_rear_link(rear_link)

    server = face_implementations.server()
    stub = face_implementations.stub(front, pool)
    return server, stub, (rear_link, fore_link, front, back)

  def tear_down_implementation(self, memo):
    rear_link, fore_link, front, back = memo
    # TODO(nathaniel): Waiting for the front and back to idle possibly should
    # not be necessary - investigate as part of graceful shutdown work.
    util.wait_for_idle(front)
    util.wait_for_idle(back)
    rear_link.stop()
    fore_link.stop()

  @unittest.skip('Service-side failure not transmitted by GRPC.')
  def testFailedUnaryRequestUnaryResponse(self):
    raise NotImplementedError()

  @unittest.skip('Service-side failure not transmitted by GRPC.')
  def testFailedUnaryRequestStreamResponse(self):
    raise NotImplementedError()

  @unittest.skip('Service-side failure not transmitted by GRPC.')
  def testFailedStreamRequestUnaryResponse(self):
    raise NotImplementedError()

  @unittest.skip('Service-side failure not transmitted by GRPC.')
  def testFailedStreamRequestStreamResponse(self):
    raise NotImplementedError()
