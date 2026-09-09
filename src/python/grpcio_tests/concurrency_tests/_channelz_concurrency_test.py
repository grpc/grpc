# Copyright 2018 The gRPC Authors
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
"""Tests of grpc_channelz.v1.channelz."""

from concurrent import futures
import logging
import unittest

import grpc
from grpc_channelz.v1 import channelz
from grpc_channelz.v1 import channelz_pb2
from grpc_channelz.v1 import channelz_pb2_grpc

from concurrency_tests._concurrency_base import ITERATIONS_PER_THREAD
from concurrency_tests._concurrency_base import RPC_TIMEOUT
from concurrency_tests._concurrency_base import ConcurrencyTestCase

_SUCCESSFUL_UNARY_UNARY = "/test/SuccessfulUnaryUnary"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"

_DISABLE_REUSE_PORT = (("grpc.so_reuseport", 0),)
_ENABLE_CHANNELZ = (("grpc.enable_channelz", 1),)

_CONTENT_PAIRS = 6


def _successful_unary_unary(request, servicer_context):
    return _RESPONSE


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _SUCCESSFUL_UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(_successful_unary_unary)
        return None


class _ChannelServerPair:
    def __init__(self):
        self.address = ""
        self.server = None
        self.channel = None
        self.server_ref_id = None
        self.channel_ref_id = None

    def start(self):
        # Server will enable channelz service
        self.server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=3),
            options=_DISABLE_REUSE_PORT + _ENABLE_CHANNELZ,
        )
        port = self.server.add_insecure_port("[::]:0")
        self.address = f"localhost:{port}"
        self.server.add_generic_rpc_handlers((_GenericHandler(),))
        self.server.start()
        self.channel = grpc.insecure_channel(
            self.address, options=_ENABLE_CHANNELZ
        )

    def bind_channelz(self, channelz_stub):
        resp = channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(start_channel_id=0)
        )
        for channel in resp.channel:
            if channel.data.target == "dns:///" + self.address:
                self.channel_ref_id = channel.ref.channel_id

        resp = channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0)
        )
        self.server_ref_id = resp.server[-1].ref.server_id

    def exec_rpc(self):
        """One RPC so the registry has a subchannel/socket for this pair"""
        self.channel.unary_unary(_SUCCESSFUL_UNARY_UNARY).with_call(
            _REQUEST, timeout=RPC_TIMEOUT
        )

    def stop(self):
        self.channel.close()
        self.server.stop(None)


def _create_channel_server_pairs(n, channelz_stub):
    pairs = [_ChannelServerPair() for i in range(n)]
    for pair in pairs:
        pair.start()
        pair.bind_channelz(channelz_stub)
        pair.exec_rpc()
    return pairs


def _destroy_channel_server_pairs(pairs):
    for pair in pairs:
        pair.stop()


class ChannelzConcurrencyTest(ConcurrencyTestCase):
    # Each querry RPC iterates the registry and serializes a JSON dump (heavy
    # operation). Decreasing the thread count here to fall into TSAN slowdown
    # time budget
    thread_count = 20

    def setUp(self):
        super().setUp()
        _, self._stub = self.start_server(
            None,
            lambda _servicer, server: channelz.add_channelz_servicer(server),
            channelz_pb2_grpc.ChannelzStub,
        )
        self._pairs = _create_channel_server_pairs(
            _CONTENT_PAIRS, self._stub
        )
        # close the pairs AFTER the base tearDown joins the workers
        self.addCleanup(_destroy_channel_server_pairs, self._pairs)

    def _query_registry(self, index):
        for i in range(ITERATIONS_PER_THREAD):
            resp = self._stub.GetTopChannels(
                channelz_pb2.GetTopChannelsRequest(start_channel_id=0),
                timeout=RPC_TIMEOUT,
            )
            self.assertTrue(len(resp.channel) > 0)

            resp = self._stub.GetServers(
                channelz_pb2.GetServersRequest(start_server_id=0),
                timeout=RPC_TIMEOUT,
            )
            self.assertTrue(len(resp.server) > 0)

            pair = self._pairs[(index + i) % len(self._pairs)]
            self._stub.GetChannel(
                channelz_pb2.GetChannelRequest(channel_id=pair.channel_ref_id),
                timeout=RPC_TIMEOUT,
            )
            self._stub.GetServer(
                channelz_pb2.GetServerRequest(server_id=pair.server_ref_id),
                timeout=RPC_TIMEOUT,
            )

    def _traffic(self, index):
        pair = self._pairs[index % len(self._pairs)]
        for _ in range(ITERATIONS_PER_THREAD):
            pair.channel.unary_unary(_SUCCESSFUL_UNARY_UNARY).with_call(
                _REQUEST, timeout=RPC_TIMEOUT
            )

    def test_concurrent_querries(self):
        self.spawn_workers(self._query_registry)

    def test_concurrent_querry_and_traffic(self):
        self.spawn_workers(self._query_registry, self._traffic)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
