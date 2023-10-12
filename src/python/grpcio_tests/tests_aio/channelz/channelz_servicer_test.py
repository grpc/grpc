# Copyright 2020 The gRPC Authors
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

import asyncio
import logging
import unittest

import grpc
from grpc.experimental import aio
from grpc_channelz.v1 import channelz
from grpc_channelz.v1 import channelz_pb2
from grpc_channelz.v1 import channelz_pb2_grpc

from tests.unit.framework.common import test_constants
from tests_aio.unit._test_base import AioTestBase

_SUCCESSFUL_UNARY_UNARY = "/test/SuccessfulUnaryUnary"
_FAILED_UNARY_UNARY = "/test/FailedUnaryUnary"
_SUCCESSFUL_STREAM_STREAM = "/test/SuccessfulStreamStream"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"

_DISABLE_REUSE_PORT = (("grpc.so_reuseport", 0),)
_ENABLE_CHANNELZ = (("grpc.enable_channelz", 1),)
_DISABLE_CHANNELZ = (("grpc.enable_channelz", 0),)

_LARGE_UNASSIGNED_ID = 10000


async def _successful_unary_unary(request, servicer_context):
    return _RESPONSE


async def _failed_unary_unary(request, servicer_context):
    servicer_context.set_code(grpc.StatusCode.INTERNAL)
    servicer_context.set_details("Channelz Test Intended Failure")


async def _successful_stream_stream(request_iterator, servicer_context):
    async for _ in request_iterator:
        yield _RESPONSE


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _SUCCESSFUL_UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(_successful_unary_unary)
        elif handler_call_details.method == _FAILED_UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(_failed_unary_unary)
        elif handler_call_details.method == _SUCCESSFUL_STREAM_STREAM:
            return grpc.stream_stream_rpc_method_handler(
                _successful_stream_stream
            )
        else:
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
        self.address = "localhost:%d" % port
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
            if channel.data.target == self.address:
                self.channel_ref_id = channel.ref.channel_id

        resp = await channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0)
        )
        self.server_ref_id = resp.server[-1].ref.server_id

    async def stop(self):
        await self.channel.close()
        await self.server.stop(None)


async def _create_channel_server_pairs(n, channelz_stub=None):
    """Create channel-server pairs."""
    pairs = [_ChannelServerPair() for i in range(n)]
    for pair in pairs:
        await pair.start()
        if channelz_stub:
            await pair.bind_channelz(channelz_stub)
    return pairs


async def _destroy_channel_server_pairs(pairs):
    for pair in pairs:
        await pair.stop()


class ChannelzServicerTest(AioTestBase):
    async def setUp(self):
        # This server is for Channelz info fetching only
        # It self should not enable Channelz
        self._server = aio.server(
            options=_DISABLE_REUSE_PORT + _DISABLE_CHANNELZ
        )
        port = self._server.add_insecure_port("[::]:0")
        channelz.add_channelz_servicer(self._server)
        await self._server.start()

        # This channel is used to fetch Channelz info only
        # Channelz should not be enabled
        self._channel = aio.insecure_channel(
            "localhost:%d" % port, options=_DISABLE_CHANNELZ
        )
        self._channelz_stub = channelz_pb2_grpc.ChannelzStub(self._channel)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def _get_server_by_ref_id(self, ref_id):
        """Server id may not be consecutive"""
        resp = await self._channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=ref_id)
        )
        self.assertEqual(ref_id, resp.server[0].ref.server_id)
        return resp.server[0]

    async def _send_successful_unary_unary(self, pair):
        call = pair.channel.unary_unary(_SUCCESSFUL_UNARY_UNARY)(_REQUEST)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def _send_failed_unary_unary(self, pair):
        try:
            await pair.channel.unary_unary(_FAILED_UNARY_UNARY)(_REQUEST)
        except grpc.RpcError:
            return
        else:
            self.fail("This call supposed to fail")

    async def _send_successful_stream_stream(self, pair):
        call = pair.channel.stream_stream(_SUCCESSFUL_STREAM_STREAM)(
            iter([_REQUEST] * test_constants.STREAM_LENGTH)
        )
        cnt = 0
        async for _ in call:
            cnt += 1
        self.assertEqual(cnt, test_constants.STREAM_LENGTH)

    async def test_get_top_channels_high_start_id(self):
        pairs = await _create_channel_server_pairs(1)

        resp = await self._channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(
                start_channel_id=_LARGE_UNASSIGNED_ID
            )
        )
        self.assertEqual(len(resp.channel), 0)
        self.assertEqual(resp.end, True)

        await _destroy_channel_server_pairs(pairs)

    async def test_successful_request(self):
        pairs = await _create_channel_server_pairs(1, self._channelz_stub)

        await self._send_successful_unary_unary(pairs[0])
        resp = await self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=pairs[0].channel_ref_id)
        )

        self.assertEqual(resp.channel.data.calls_started, 1)
        self.assertEqual(resp.channel.data.calls_succeeded, 1)
        self.assertEqual(resp.channel.data.calls_failed, 0)

        await _destroy_channel_server_pairs(pairs)

    async def test_failed_request(self):
        pairs = await _create_channel_server_pairs(1, self._channelz_stub)

        await self._send_failed_unary_unary(pairs[0])
        resp = await self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=pairs[0].channel_ref_id)
        )
        self.assertEqual(resp.channel.data.calls_started, 1)
        self.assertEqual(resp.channel.data.calls_succeeded, 0)
        self.assertEqual(resp.channel.data.calls_failed, 1)

        await _destroy_channel_server_pairs(pairs)

    async def test_many_requests(self):
        pairs = await _create_channel_server_pairs(1, self._channelz_stub)

        k_success = 7
        k_failed = 9
        for i in range(k_success):
            await self._send_successful_unary_unary(pairs[0])
        for i in range(k_failed):
            await self._send_failed_unary_unary(pairs[0])
        resp = await self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=pairs[0].channel_ref_id)
        )
        self.assertEqual(resp.channel.data.calls_started, k_success + k_failed)
        self.assertEqual(resp.channel.data.calls_succeeded, k_success)
        self.assertEqual(resp.channel.data.calls_failed, k_failed)

        await _destroy_channel_server_pairs(pairs)

    async def test_many_requests_many_channel(self):
        k_channels = 4
        pairs = await _create_channel_server_pairs(
            k_channels, self._channelz_stub
        )
        k_success = 11
        k_failed = 13
        for i in range(k_success):
            await self._send_successful_unary_unary(pairs[0])
            await self._send_successful_unary_unary(pairs[2])
        for i in range(k_failed):
            await self._send_failed_unary_unary(pairs[1])
            await self._send_failed_unary_unary(pairs[2])

        # The first channel saw only successes
        resp = await self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=pairs[0].channel_ref_id)
        )
        self.assertEqual(resp.channel.data.calls_started, k_success)
        self.assertEqual(resp.channel.data.calls_succeeded, k_success)
        self.assertEqual(resp.channel.data.calls_failed, 0)

        # The second channel saw only failures
        resp = await self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=pairs[1].channel_ref_id)
        )
        self.assertEqual(resp.channel.data.calls_started, k_failed)
        self.assertEqual(resp.channel.data.calls_succeeded, 0)
        self.assertEqual(resp.channel.data.calls_failed, k_failed)

        # The third channel saw both successes and failures
        resp = await self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=pairs[2].channel_ref_id)
        )
        self.assertEqual(resp.channel.data.calls_started, k_success + k_failed)
        self.assertEqual(resp.channel.data.calls_succeeded, k_success)
        self.assertEqual(resp.channel.data.calls_failed, k_failed)

        # The fourth channel saw nothing
        resp = await self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=pairs[3].channel_ref_id)
        )
        self.assertEqual(resp.channel.data.calls_started, 0)
        self.assertEqual(resp.channel.data.calls_succeeded, 0)
        self.assertEqual(resp.channel.data.calls_failed, 0)

        await _destroy_channel_server_pairs(pairs)

    async def test_many_subchannels(self):
        k_channels = 4
        pairs = await _create_channel_server_pairs(
            k_channels, self._channelz_stub
        )
        k_success = 17
        k_failed = 19
        for i in range(k_success):
            await self._send_successful_unary_unary(pairs[0])
            await self._send_successful_unary_unary(pairs[2])
        for i in range(k_failed):
            await self._send_failed_unary_unary(pairs[1])
            await self._send_failed_unary_unary(pairs[2])

        for i in range(k_channels):
            gc_resp = await self._channelz_stub.GetChannel(
                channelz_pb2.GetChannelRequest(
                    channel_id=pairs[i].channel_ref_id
                )
            )
            # If no call performed in the channel, there shouldn't be any subchannel
            if gc_resp.channel.data.calls_started == 0:
                self.assertEqual(len(gc_resp.channel.subchannel_ref), 0)
                continue

            # Otherwise, the subchannel should exist
            self.assertGreater(len(gc_resp.channel.subchannel_ref), 0)
            gsc_resp = await self._channelz_stub.GetSubchannel(
                channelz_pb2.GetSubchannelRequest(
                    subchannel_id=gc_resp.channel.subchannel_ref[
                        0
                    ].subchannel_id
                )
            )
            self.assertEqual(
                gc_resp.channel.data.calls_started,
                gsc_resp.subchannel.data.calls_started,
            )
            self.assertEqual(
                gc_resp.channel.data.calls_succeeded,
                gsc_resp.subchannel.data.calls_succeeded,
            )
            self.assertEqual(
                gc_resp.channel.data.calls_failed,
                gsc_resp.subchannel.data.calls_failed,
            )

        await _destroy_channel_server_pairs(pairs)

    async def test_server_call(self):
        pairs = await _create_channel_server_pairs(1, self._channelz_stub)

        k_success = 23
        k_failed = 29
        for i in range(k_success):
            await self._send_successful_unary_unary(pairs[0])
        for i in range(k_failed):
            await self._send_failed_unary_unary(pairs[0])

        resp = await self._get_server_by_ref_id(pairs[0].server_ref_id)
        self.assertEqual(resp.data.calls_started, k_success + k_failed)
        self.assertEqual(resp.data.calls_succeeded, k_success)
        self.assertEqual(resp.data.calls_failed, k_failed)

        await _destroy_channel_server_pairs(pairs)

    async def test_many_subchannels_and_sockets(self):
        k_channels = 4
        pairs = await _create_channel_server_pairs(
            k_channels, self._channelz_stub
        )
        k_success = 3
        k_failed = 5
        for i in range(k_success):
            await self._send_successful_unary_unary(pairs[0])
            await self._send_successful_unary_unary(pairs[2])
        for i in range(k_failed):
            await self._send_failed_unary_unary(pairs[1])
            await self._send_failed_unary_unary(pairs[2])

        for i in range(k_channels):
            gc_resp = await self._channelz_stub.GetChannel(
                channelz_pb2.GetChannelRequest(
                    channel_id=pairs[i].channel_ref_id
                )
            )

            # If no call performed in the channel, there shouldn't be any subchannel
            if gc_resp.channel.data.calls_started == 0:
                self.assertEqual(len(gc_resp.channel.subchannel_ref), 0)
                continue

            # Otherwise, the subchannel should exist
            self.assertGreater(len(gc_resp.channel.subchannel_ref), 0)
            gsc_resp = await self._channelz_stub.GetSubchannel(
                channelz_pb2.GetSubchannelRequest(
                    subchannel_id=gc_resp.channel.subchannel_ref[
                        0
                    ].subchannel_id
                )
            )
            self.assertEqual(len(gsc_resp.subchannel.socket_ref), 1)

            gs_resp = await self._channelz_stub.GetSocket(
                channelz_pb2.GetSocketRequest(
                    socket_id=gsc_resp.subchannel.socket_ref[0].socket_id
                )
            )
            self.assertEqual(
                gsc_resp.subchannel.data.calls_started,
                gs_resp.socket.data.streams_started,
            )
            self.assertEqual(0, gs_resp.socket.data.streams_failed)
            # Calls started == messages sent, only valid for unary calls
            self.assertEqual(
                gsc_resp.subchannel.data.calls_started,
                gs_resp.socket.data.messages_sent,
            )

        await _destroy_channel_server_pairs(pairs)

    async def test_streaming_rpc(self):
        pairs = await _create_channel_server_pairs(1, self._channelz_stub)
        # In C++, the argument for _send_successful_stream_stream is message length.
        # Here the argument is still channel idx, to be consistent with the other two.
        await self._send_successful_stream_stream(pairs[0])

        gc_resp = await self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=pairs[0].channel_ref_id)
        )
        self.assertEqual(gc_resp.channel.data.calls_started, 1)
        self.assertEqual(gc_resp.channel.data.calls_succeeded, 1)
        self.assertEqual(gc_resp.channel.data.calls_failed, 0)
        # Subchannel exists
        self.assertGreater(len(gc_resp.channel.subchannel_ref), 0)

        while True:
            gsc_resp = await self._channelz_stub.GetSubchannel(
                channelz_pb2.GetSubchannelRequest(
                    subchannel_id=gc_resp.channel.subchannel_ref[
                        0
                    ].subchannel_id
                )
            )
            if (
                gsc_resp.subchannel.data.calls_started
                == gsc_resp.subchannel.data.calls_succeeded
                + gsc_resp.subchannel.data.calls_failed
            ):
                break
        self.assertEqual(gsc_resp.subchannel.data.calls_started, 1)
        self.assertEqual(gsc_resp.subchannel.data.calls_failed, 0)
        self.assertEqual(gsc_resp.subchannel.data.calls_succeeded, 1)
        # Socket exists
        self.assertEqual(len(gsc_resp.subchannel.socket_ref), 1)

        while True:
            gs_resp = await self._channelz_stub.GetSocket(
                channelz_pb2.GetSocketRequest(
                    socket_id=gsc_resp.subchannel.socket_ref[0].socket_id
                )
            )
            if (
                gs_resp.socket.data.streams_started
                == gs_resp.socket.data.streams_succeeded
                + gs_resp.socket.data.streams_failed
            ):
                break
        self.assertEqual(gs_resp.socket.data.streams_started, 1)
        self.assertEqual(gs_resp.socket.data.streams_failed, 0)
        self.assertEqual(gs_resp.socket.data.streams_succeeded, 1)
        self.assertEqual(
            gs_resp.socket.data.messages_sent, test_constants.STREAM_LENGTH
        )
        self.assertEqual(
            gs_resp.socket.data.messages_received, test_constants.STREAM_LENGTH
        )

        await _destroy_channel_server_pairs(pairs)

    async def test_server_sockets(self):
        pairs = await _create_channel_server_pairs(1, self._channelz_stub)

        await self._send_successful_unary_unary(pairs[0])
        await self._send_failed_unary_unary(pairs[0])

        resp = await self._get_server_by_ref_id(pairs[0].server_ref_id)
        self.assertEqual(resp.data.calls_started, 2)
        self.assertEqual(resp.data.calls_succeeded, 1)
        self.assertEqual(resp.data.calls_failed, 1)

        gss_resp = await self._channelz_stub.GetServerSockets(
            channelz_pb2.GetServerSocketsRequest(
                server_id=resp.ref.server_id, start_socket_id=0
            )
        )
        # If the RPC call failed, it will raise a grpc.RpcError
        # So, if there is no exception raised, considered pass
        await _destroy_channel_server_pairs(pairs)

    async def test_server_listen_sockets(self):
        pairs = await _create_channel_server_pairs(1, self._channelz_stub)

        resp = await self._get_server_by_ref_id(pairs[0].server_ref_id)
        self.assertEqual(len(resp.listen_socket), 1)

        gs_resp = await self._channelz_stub.GetSocket(
            channelz_pb2.GetSocketRequest(
                socket_id=resp.listen_socket[0].socket_id
            )
        )
        # If the RPC call failed, it will raise a grpc.RpcError
        # So, if there is no exception raised, considered pass
        await _destroy_channel_server_pairs(pairs)

    async def test_invalid_query_get_server(self):
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._channelz_stub.GetServer(
                channelz_pb2.GetServerRequest(server_id=_LARGE_UNASSIGNED_ID)
            )
        self.assertEqual(
            grpc.StatusCode.NOT_FOUND, exception_context.exception.code()
        )

    async def test_invalid_query_get_channel(self):
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._channelz_stub.GetChannel(
                channelz_pb2.GetChannelRequest(channel_id=_LARGE_UNASSIGNED_ID)
            )
        self.assertEqual(
            grpc.StatusCode.NOT_FOUND, exception_context.exception.code()
        )

    async def test_invalid_query_get_subchannel(self):
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._channelz_stub.GetSubchannel(
                channelz_pb2.GetSubchannelRequest(
                    subchannel_id=_LARGE_UNASSIGNED_ID
                )
            )
        self.assertEqual(
            grpc.StatusCode.NOT_FOUND, exception_context.exception.code()
        )

    async def test_invalid_query_get_socket(self):
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._channelz_stub.GetSocket(
                channelz_pb2.GetSocketRequest(socket_id=_LARGE_UNASSIGNED_ID)
            )
        self.assertEqual(
            grpc.StatusCode.NOT_FOUND, exception_context.exception.code()
        )

    async def test_invalid_query_get_server_sockets(self):
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._channelz_stub.GetServerSockets(
                channelz_pb2.GetServerSocketsRequest(
                    server_id=_LARGE_UNASSIGNED_ID,
                    start_socket_id=0,
                )
            )
        self.assertEqual(
            grpc.StatusCode.NOT_FOUND, exception_context.exception.code()
        )


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
