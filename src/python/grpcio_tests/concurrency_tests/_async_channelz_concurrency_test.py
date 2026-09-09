# Copyright 2026 The gRPC Authors
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
"""Concurrency tests for AsyncIO grpc_channelz.v1.channelz."""

import logging
import unittest

import grpc
from grpc.experimental import aio
from grpc_channelz.v1 import channelz
from grpc_channelz.v1 import channelz_pb2
from grpc_channelz.v1 import channelz_pb2_grpc

from concurrency_tests._async_concurrency_base import ITERATIONS_PER_TASK
from concurrency_tests._async_concurrency_base import RPC_TIMEOUT
from concurrency_tests._async_concurrency_base import AsyncConcurrencyTestCase

_SUCCESSFUL_UNARY_UNARY = "/test/SuccessfulUnaryUnary"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"

_DISABLE_REUSE_PORT = (("grpc.so_reuseport", 0),)
_ENABLE_CHANNELZ = (("grpc.enable_channelz", 1),)

_CONTENT_PAIRS = 6


async def _successful_unary_unary(request, servicer_context):
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

    async def start(self):
        # Server will enable channelz service
        self.server = aio.server(options=_DISABLE_REUSE_PORT + _ENABLE_CHANNELZ)
        port = self.server.add_insecure_port("[::]:0")
        self.address = f"localhost:{port}"
        self.server.add_generic_rpc_handlers((_GenericHandler(),))
        await self.server.start()

        # Channel will enable channelz service...
        self.channel = aio.insecure_channel(
            self.address, options=_ENABLE_CHANNELZ
        )

    async def bind_channelz(self, channelz_stub):
        resp = await channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(start_channel_id=0)
        )
        for channel in resp.channel:
            if channel.data.target == "dns:///" + self.address:
                self.channel_ref_id = channel.ref.channel_id

        resp = await channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0)
        )
        self.server_ref_id = resp.server[-1].ref.server_id

    async def exec_rpc(self):
        """One RPC so the registry has a subchannel/socket for this pair"""
        self.channel.unary_unary(_SUCCESSFUL_UNARY_UNARY)(
            _REQUEST, timeout=RPC_TIMEOUT
        )

    async def stop(self):
        await self.channel.close()
        await self.server.stop(None)


async def _create_channel_server_pairs(n, channelz_stub):
    """Create channel-server pairs."""
    pairs = [_ChannelServerPair() for i in range(n)]
    for pair in pairs:
        await pair.start()
        await pair.bind_channelz(channelz_stub)
        await pair.exec_rpc()
    return pairs


async def _destroy_channel_server_pairs(pairs):
    for pair in pairs:
        await pair.stop()


class AsyncCsdsConcurrencyTest(AsyncConcurrencyTestCase):
    # Each querry RPC iterates the registry and serializes a JSON dump (heavy
    # operation). Decreasing the future count here to fall into TSAN slowdown
    # time budget
    concurrency = 20

    async def setUp(self):
        await super().setUp()
        _, self._stub = await self.start_server(
            None,
            lambda _servicer, server: channelz.add_channelz_servicer(server),
            channelz_pb2_grpc.ChannelzStub,
        )
        self._pairs = await _create_channel_server_pairs(
            _CONTENT_PAIRS, self._stub
        )

    async def tearDown(self):
        await _destroy_channel_server_pairs(self._pairs)
        await super().tearDown()

    async def _query_registry(self, index):
        for i in range(ITERATIONS_PER_TASK):
            top_channels_resp = await self._stub.GetTopChannels(
                channelz_pb2.GetTopChannelsRequest(start_channel_id=0),
                timeout=RPC_TIMEOUT,
            )
            self.assertTrue(len(top_channels_resp.channel) > 0)

            servers_resp = await self._stub.GetServers(
                channelz_pb2.GetServersRequest(start_server_id=0),
                timeout=RPC_TIMEOUT,
            )
            self.assertTrue(len(servers_resp.server) > 0)

            pair = self._pairs[(index + i) % len(self._pairs)]
            await self._stub.GetChannel(
                channelz_pb2.GetChannelRequest(channel_id=pair.channel_ref_id),
                timeout=RPC_TIMEOUT,
            )
            await self._stub.GetServer(
                channelz_pb2.GetServerRequest(server_id=pair.server_ref_id),
                timeout=RPC_TIMEOUT,
            )

    async def _traffic(self, index):
        pair = self._pairs[index % len(self._pairs)]
        for _ in range(ITERATIONS_PER_TASK):
            await pair.channel.unary_unary(_SUCCESSFUL_UNARY_UNARY)(
                _REQUEST, timeout=RPC_TIMEOUT
            )

    async def test_concurrent_querries(self):
        await self.run_workers(self._query_registry)

    async def test_concurrent_querry_and_traffic(self):
        await self.run_workers(self._query_registry, self._traffic)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
