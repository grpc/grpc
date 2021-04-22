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

import unittest
from concurrent import futures
from src.proto.grpc.testing import empty_pb2, messages_pb2, test_pb2_grpc
import grpc
import gevent
import sys
from gevent.pool import Group
from tests_gevent.unit._test_server import start_test_server, UNARY_CALL_WITH_SLEEP_VALUE

_UNARY_CALL_METHOD_WITH_SLEEP = '/grpc.testing.TestService/UnaryCallWithSleep'


class CloseChannelTest(unittest.TestCase):

    def setUp(self):
        self._server_target, self._server = start_test_server()
        self._channel = grpc.insecure_channel(self._server_target)
        self._channel_closed = False
        self._unhandled_greenlet_exit = False
        sys.excepthook = self._global_exception_handler

    def tearDown(self):
        self._server.stop(None)
        self._channel.close()

    def test_graceful_close(self):
        UnaryCallWithSleep = self._channel.unary_unary(
            _UNARY_CALL_METHOD_WITH_SLEEP,
            request_serializer=messages_pb2.SimpleRequest.SerializeToString,
            response_deserializer=messages_pb2.SimpleResponse.FromString,
        )
        _, response = UnaryCallWithSleep.with_call(messages_pb2.SimpleRequest())
        self._channel.close()

        self.assertEqual(grpc.StatusCode.OK, response.code())

    def test_graceful_close_in_greenlet(self):
        group = Group()
        greenlet = group.spawn(self._run_client)
        gevent.sleep(UNARY_CALL_WITH_SLEEP_VALUE * 2)
        self._channel_closed = True
        gevent.sleep(UNARY_CALL_WITH_SLEEP_VALUE)
        self._channel.close()
        group.killone(greenlet)
        self.assertFalse(self._unhandled_greenlet_exit,
                         "Unhandled GreenletExit")

    def test_ungraceful_close_in_greenlet(self):
        group = Group()
        greenlet = group.spawn(self._run_client)
        gevent.sleep(UNARY_CALL_WITH_SLEEP_VALUE * 2)
        group.killone(greenlet)
        gevent.sleep(UNARY_CALL_WITH_SLEEP_VALUE)
        self.assertFalse(self._unhandled_greenlet_exit,
                         "Unhandled GreenletExit")

    def _run_client(self):
        UnaryCallWithSleep = self._channel.unary_unary(
            _UNARY_CALL_METHOD_WITH_SLEEP,
            request_serializer=messages_pb2.SimpleRequest.SerializeToString,
            response_deserializer=messages_pb2.SimpleResponse.FromString,
        )
        while not self._channel_closed:
            _, response = UnaryCallWithSleep.with_call(
                messages_pb2.SimpleRequest())

    def _global_exception_handler(self, exctype, value, tb):
        if exctype == gevent.GreenletExit:
            self._channel_closed = True
            gevent.sleep(UNARY_CALL_WITH_SLEEP_VALUE)
            self._channel.close()
            self._unhandled_greenlet_exit = True
            return
        sys.__excepthook__(exctype, value, tb)


if __name__ == '__main__':
    unittest.main(verbosity=2)
