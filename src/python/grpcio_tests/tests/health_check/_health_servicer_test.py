# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Tests of grpc_health.v1.health."""

import unittest

import grpc
from grpc.framework.foundation import logging_pool
from grpc_health.v1 import health
from grpc_health.v1 import health_pb2
from grpc_health.v1 import health_pb2_grpc

from tests.unit.framework.common import test_constants


class HealthServicerTest(unittest.TestCase):

    def setUp(self):
        servicer = health.HealthServicer()
        servicer.set('', health_pb2.HealthCheckResponse.SERVING)
        servicer.set('grpc.test.TestServiceServing',
                     health_pb2.HealthCheckResponse.SERVING)
        servicer.set('grpc.test.TestServiceUnknown',
                     health_pb2.HealthCheckResponse.UNKNOWN)
        servicer.set('grpc.test.TestServiceNotServing',
                     health_pb2.HealthCheckResponse.NOT_SERVING)
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        self._server = grpc.server(server_pool)
        port = self._server.add_insecure_port('[::]:0')
        health_pb2_grpc.add_HealthServicer_to_server(servicer, self._server)
        self._server.start()

        channel = grpc.insecure_channel('localhost:%d' % port)
        self._stub = health_pb2_grpc.HealthStub(channel)

    def test_empty_service(self):
        request = health_pb2.HealthCheckRequest()
        resp = self._stub.Check(request)
        self.assertEqual(health_pb2.HealthCheckResponse.SERVING, resp.status)

    def test_serving_service(self):
        request = health_pb2.HealthCheckRequest(
            service='grpc.test.TestServiceServing')
        resp = self._stub.Check(request)
        self.assertEqual(health_pb2.HealthCheckResponse.SERVING, resp.status)

    def test_unknown_serivce(self):
        request = health_pb2.HealthCheckRequest(
            service='grpc.test.TestServiceUnknown')
        resp = self._stub.Check(request)
        self.assertEqual(health_pb2.HealthCheckResponse.UNKNOWN, resp.status)

    def test_not_serving_service(self):
        request = health_pb2.HealthCheckRequest(
            service='grpc.test.TestServiceNotServing')
        resp = self._stub.Check(request)
        self.assertEqual(health_pb2.HealthCheckResponse.NOT_SERVING,
                         resp.status)

    def test_not_found_service(self):
        request = health_pb2.HealthCheckRequest(service='not-found')
        with self.assertRaises(grpc.RpcError) as context:
            resp = self._stub.Check(request)

        self.assertEqual(grpc.StatusCode.NOT_FOUND, context.exception.code())


if __name__ == '__main__':
    unittest.main(verbosity=2)
