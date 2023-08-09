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

import logging
import queue
import sys
import threading
import time
import unittest

import grpc
from grpc_health.v1 import health
from grpc_health.v1 import health_pb2
from grpc_health.v1 import health_pb2_grpc

from tests.unit import test_common
from tests.unit import thread_pool
from tests.unit.framework.common import test_constants

_SERVING_SERVICE = "grpc.test.TestServiceServing"
_UNKNOWN_SERVICE = "grpc.test.TestServiceUnknown"
_NOT_SERVING_SERVICE = "grpc.test.TestServiceNotServing"
_WATCH_SERVICE = "grpc.test.WatchService"


def _consume_responses(response_iterator, response_queue):
    for response in response_iterator:
        response_queue.put(response)


class BaseWatchTests(object):
    @unittest.skipIf(
        sys.version_info[0] < 3, "ProtoBuf descriptor has moved on from Python2"
    )
    class WatchTests(unittest.TestCase):
        def start_server(self, non_blocking=False, thread_pool=None):
            self._thread_pool = thread_pool
            self._servicer = health.HealthServicer(
                experimental_non_blocking=non_blocking,
                experimental_thread_pool=thread_pool,
            )
            self._servicer.set(
                _SERVING_SERVICE, health_pb2.HealthCheckResponse.SERVING
            )
            self._servicer.set(
                _UNKNOWN_SERVICE, health_pb2.HealthCheckResponse.UNKNOWN
            )
            self._servicer.set(
                _NOT_SERVING_SERVICE, health_pb2.HealthCheckResponse.NOT_SERVING
            )
            self._server = test_common.test_server()
            port = self._server.add_insecure_port("[::]:0")
            health_pb2_grpc.add_HealthServicer_to_server(
                self._servicer, self._server
            )
            self._server.start()

            self._channel = grpc.insecure_channel("localhost:%d" % port)
            self._stub = health_pb2_grpc.HealthStub(self._channel)

        def tearDown(self):
            self._server.stop(None)
            self._channel.close()

        def test_watch_empty_service(self):
            request = health_pb2.HealthCheckRequest(service="")
            response_queue = queue.Queue()
            rendezvous = self._stub.Watch(request)
            thread = threading.Thread(
                target=_consume_responses, args=(rendezvous, response_queue)
            )
            thread.start()

            response = response_queue.get(timeout=test_constants.SHORT_TIMEOUT)
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVING, response.status
            )

            rendezvous.cancel()
            thread.join()
            self.assertTrue(response_queue.empty())

            if self._thread_pool is not None:
                self.assertTrue(self._thread_pool.was_used())

        def test_watch_new_service(self):
            request = health_pb2.HealthCheckRequest(service=_WATCH_SERVICE)
            response_queue = queue.Queue()
            rendezvous = self._stub.Watch(request)
            thread = threading.Thread(
                target=_consume_responses, args=(rendezvous, response_queue)
            )
            thread.start()

            response = response_queue.get(timeout=test_constants.SHORT_TIMEOUT)
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVICE_UNKNOWN, response.status
            )

            self._servicer.set(
                _WATCH_SERVICE, health_pb2.HealthCheckResponse.SERVING
            )
            response = response_queue.get(timeout=test_constants.SHORT_TIMEOUT)
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVING, response.status
            )

            self._servicer.set(
                _WATCH_SERVICE, health_pb2.HealthCheckResponse.NOT_SERVING
            )
            response = response_queue.get(timeout=test_constants.SHORT_TIMEOUT)
            self.assertEqual(
                health_pb2.HealthCheckResponse.NOT_SERVING, response.status
            )

            rendezvous.cancel()
            thread.join()
            self.assertTrue(response_queue.empty())

        def test_watch_service_isolation(self):
            request = health_pb2.HealthCheckRequest(service=_WATCH_SERVICE)
            response_queue = queue.Queue()
            rendezvous = self._stub.Watch(request)
            thread = threading.Thread(
                target=_consume_responses, args=(rendezvous, response_queue)
            )
            thread.start()

            response = response_queue.get(timeout=test_constants.SHORT_TIMEOUT)
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVICE_UNKNOWN, response.status
            )

            self._servicer.set(
                "some-other-service", health_pb2.HealthCheckResponse.SERVING
            )
            with self.assertRaises(queue.Empty):
                response_queue.get(timeout=test_constants.SHORT_TIMEOUT)

            rendezvous.cancel()
            thread.join()
            self.assertTrue(response_queue.empty())

        def test_two_watchers(self):
            request = health_pb2.HealthCheckRequest(service=_WATCH_SERVICE)
            response_queue1 = queue.Queue()
            response_queue2 = queue.Queue()
            rendezvous1 = self._stub.Watch(request)
            rendezvous2 = self._stub.Watch(request)
            thread1 = threading.Thread(
                target=_consume_responses, args=(rendezvous1, response_queue1)
            )
            thread2 = threading.Thread(
                target=_consume_responses, args=(rendezvous2, response_queue2)
            )
            thread1.start()
            thread2.start()

            response1 = response_queue1.get(
                timeout=test_constants.SHORT_TIMEOUT
            )
            response2 = response_queue2.get(
                timeout=test_constants.SHORT_TIMEOUT
            )
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVICE_UNKNOWN, response1.status
            )
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVICE_UNKNOWN, response2.status
            )

            self._servicer.set(
                _WATCH_SERVICE, health_pb2.HealthCheckResponse.SERVING
            )
            response1 = response_queue1.get(
                timeout=test_constants.SHORT_TIMEOUT
            )
            response2 = response_queue2.get(
                timeout=test_constants.SHORT_TIMEOUT
            )
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVING, response1.status
            )
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVING, response2.status
            )

            rendezvous1.cancel()
            rendezvous2.cancel()
            thread1.join()
            thread2.join()
            self.assertTrue(response_queue1.empty())
            self.assertTrue(response_queue2.empty())

        @unittest.skip("https://github.com/grpc/grpc/issues/18127")
        def test_cancelled_watch_removed_from_watch_list(self):
            request = health_pb2.HealthCheckRequest(service=_WATCH_SERVICE)
            response_queue = queue.Queue()
            rendezvous = self._stub.Watch(request)
            thread = threading.Thread(
                target=_consume_responses, args=(rendezvous, response_queue)
            )
            thread.start()

            response = response_queue.get(timeout=test_constants.SHORT_TIMEOUT)
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVICE_UNKNOWN, response.status
            )

            rendezvous.cancel()
            self._servicer.set(
                _WATCH_SERVICE, health_pb2.HealthCheckResponse.SERVING
            )
            thread.join()

            # Wait, if necessary, for serving thread to process client cancellation
            timeout = time.time() + test_constants.TIME_ALLOWANCE
            while (
                time.time() < timeout
                and self._servicer._send_response_callbacks[_WATCH_SERVICE]
            ):
                time.sleep(1)
            self.assertFalse(
                self._servicer._send_response_callbacks[_WATCH_SERVICE],
                "watch set should be empty",
            )
            self.assertTrue(response_queue.empty())

        def test_graceful_shutdown(self):
            request = health_pb2.HealthCheckRequest(service="")
            response_queue = queue.Queue()
            rendezvous = self._stub.Watch(request)
            thread = threading.Thread(
                target=_consume_responses, args=(rendezvous, response_queue)
            )
            thread.start()

            response = response_queue.get(timeout=test_constants.SHORT_TIMEOUT)
            self.assertEqual(
                health_pb2.HealthCheckResponse.SERVING, response.status
            )

            self._servicer.enter_graceful_shutdown()
            response = response_queue.get(timeout=test_constants.SHORT_TIMEOUT)
            self.assertEqual(
                health_pb2.HealthCheckResponse.NOT_SERVING, response.status
            )

            # This should be a no-op.
            self._servicer.set("", health_pb2.HealthCheckResponse.SERVING)

            rendezvous.cancel()
            thread.join()
            self.assertTrue(response_queue.empty())


@unittest.skipIf(
    sys.version_info[0] < 3, "ProtoBuf descriptor has moved on from Python2"
)
class HealthServicerTest(BaseWatchTests.WatchTests):
    def setUp(self):
        self._thread_pool = thread_pool.RecordingThreadPool(max_workers=None)
        super(HealthServicerTest, self).start_server(
            non_blocking=True, thread_pool=self._thread_pool
        )

    def test_check_empty_service(self):
        request = health_pb2.HealthCheckRequest()
        resp = self._stub.Check(request)
        self.assertEqual(health_pb2.HealthCheckResponse.SERVING, resp.status)

    def test_check_serving_service(self):
        request = health_pb2.HealthCheckRequest(service=_SERVING_SERVICE)
        resp = self._stub.Check(request)
        self.assertEqual(health_pb2.HealthCheckResponse.SERVING, resp.status)

    def test_check_unknown_service(self):
        request = health_pb2.HealthCheckRequest(service=_UNKNOWN_SERVICE)
        resp = self._stub.Check(request)
        self.assertEqual(health_pb2.HealthCheckResponse.UNKNOWN, resp.status)

    def test_check_not_serving_service(self):
        request = health_pb2.HealthCheckRequest(service=_NOT_SERVING_SERVICE)
        resp = self._stub.Check(request)
        self.assertEqual(
            health_pb2.HealthCheckResponse.NOT_SERVING, resp.status
        )

    def test_check_not_found_service(self):
        request = health_pb2.HealthCheckRequest(service="not-found")
        with self.assertRaises(grpc.RpcError) as context:
            resp = self._stub.Check(request)

        self.assertEqual(grpc.StatusCode.NOT_FOUND, context.exception.code())

    def test_health_service_name(self):
        self.assertEqual(health.SERVICE_NAME, "grpc.health.v1.Health")


@unittest.skipIf(
    sys.version_info[0] < 3, "ProtoBuf descriptor has moved on from Python2"
)
class HealthServicerBackwardsCompatibleWatchTest(BaseWatchTests.WatchTests):
    def setUp(self):
        super(HealthServicerBackwardsCompatibleWatchTest, self).start_server(
            non_blocking=False, thread_pool=None
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
