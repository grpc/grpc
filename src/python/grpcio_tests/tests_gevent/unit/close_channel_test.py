# Copyright 2021 The gRPC Authors
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

import sys
import unittest

import gevent
from gevent.pool import Group
import grpc

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests_gevent.unit._test_server import start_test_server

_UNARY_CALL_METHOD_WITH_SLEEP = "/grpc.testing.TestService/UnaryCallWithSleep"


class CloseChannelTest(unittest.TestCase):
    def setUp(self):
        self._server_target, self._server = start_test_server()
        self._channel = grpc.insecure_channel(self._server_target)
        self._unhandled_exception = False
        sys.excepthook = self._global_exception_handler

    def tearDown(self):
        self._channel.close()
        self._server.stop(None)

    def test_graceful_close(self):
        stub = test_pb2_grpc.TestServiceStub(self._channel)
        _, response = stub.UnaryCall.with_call(messages_pb2.SimpleRequest())

        self._channel.close()

        self.assertEqual(grpc.StatusCode.OK, response.code())

    def test_graceful_close_in_greenlet(self):
        group = Group()
        stub = test_pb2_grpc.TestServiceStub(self._channel)
        greenlet = group.spawn(self._run_client, stub.UnaryCall)
        # release loop so that greenlet can take control
        gevent.sleep()
        self._channel.close()
        group.killone(greenlet)
        self.assertFalse(self._unhandled_exception, "Unhandled GreenletExit")
        try:
            greenlet.get()
        except Exception as e:  # pylint: disable=broad-except
            self.fail(f"Unexpected exception in greenlet: {e}")

    def test_ungraceful_close_in_greenlet(self):
        group = Group()
        UnaryCallWithSleep = self._channel.unary_unary(
            _UNARY_CALL_METHOD_WITH_SLEEP,
            request_serializer=messages_pb2.SimpleRequest.SerializeToString,
            response_deserializer=messages_pb2.SimpleResponse.FromString,
        )
        greenlet = group.spawn(self._run_client, UnaryCallWithSleep)
        # release loop so that greenlet can take control
        gevent.sleep()
        group.killone(greenlet)
        self.assertFalse(self._unhandled_exception, "Unhandled GreenletExit")

    def test_kill_greenlet_with_generic_exception(self):
        group = Group()
        UnaryCallWithSleep = self._channel.unary_unary(
            _UNARY_CALL_METHOD_WITH_SLEEP,
            request_serializer=messages_pb2.SimpleRequest.SerializeToString,
            response_deserializer=messages_pb2.SimpleResponse.FromString,
        )
        greenlet = group.spawn(self._run_client, UnaryCallWithSleep)
        # release loop so that greenlet can take control
        gevent.sleep()
        group.killone(greenlet, exception=Exception)
        self.assertFalse(self._unhandled_exception, "Unhandled exception")
        self.assertRaises(Exception, greenlet.get)

    def _run_client(self, call):
        try:
            call.with_call(messages_pb2.SimpleRequest())
        except grpc.RpcError as e:
            if e.code() != grpc.StatusCode.CANCELLED:
                raise

    def _global_exception_handler(self, exctype, value, tb):
        if exctype == gevent.GreenletExit:
            self._unhandled_exception = True
            return
        sys.__excepthook__(exctype, value, tb)


if __name__ == "__main__":
    unittest.main(verbosity=2)
