# Copyright 2016, Google Inc.
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

"""Tests of grpc_health_checking.health.v1.health."""

import unittest

from grpc_health_checking.health.v1 import health
from grpc_health_checking.health.v1 import health_pb2


class HealthServicerTest(unittest.TestCase):

  def setUp(self):
    self.servicer = health.HealthServicer()
    self.servicer.set('', health_pb2.HealthCheckResponse.SERVING)
    self.servicer.set('grpc.test.TestServiceServing',
                      health_pb2.HealthCheckResponse.SERVING)
    self.servicer.set('grpc.test.TestServiceUnknown',
                      health_pb2.HealthCheckResponse.UNKNOWN)
    self.servicer.set('grpc.test.TestServiceNotServing',
                      health_pb2.HealthCheckResponse.NOT_SERVING)

  def test_empty_service(self):
    request = health_pb2.HealthCheckRequest()
    resp = self.servicer.Check(request, None)
    self.assertEqual(resp.status, health_pb2.HealthCheckResponse.SERVING)

  def test_serving_service(self):
    request = health_pb2.HealthCheckRequest(
        service='grpc.test.TestServiceServing')
    resp = self.servicer.Check(request, None)
    self.assertEqual(resp.status, health_pb2.HealthCheckResponse.SERVING)

  def test_unknown_serivce(self):
    request = health_pb2.HealthCheckRequest(
        service='grpc.test.TestServiceUnknown')
    resp = self.servicer.Check(request, None)
    self.assertEqual(resp.status, health_pb2.HealthCheckResponse.UNKNOWN)

  def test_not_serving_service(self):
    request = health_pb2.HealthCheckRequest(
        service='grpc.test.TestServiceNotServing')
    resp = self.servicer.Check(request, None)
    self.assertEqual(resp.status, health_pb2.HealthCheckResponse.NOT_SERVING)


if __name__ == '__main__':
  unittest.main(verbosity=2)
