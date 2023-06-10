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
import sys
import unittest

import grpc
from grpc_channelz.v1 import channelz
from grpc_channelz.v1 import channelz_pb2
from grpc_channelz.v1 import channelz_pb2_grpc

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_SUCCESSFUL_UNARY_UNARY = "/test/SuccessfulUnaryUnary"
_FAILED_UNARY_UNARY = "/test/FailedUnaryUnary"
_SUCCESSFUL_STREAM_STREAM = "/test/SuccessfulStreamStream"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"

_DISABLE_REUSE_PORT = (("grpc.so_reuseport", 0),)
_ENABLE_CHANNELZ = (("grpc.enable_channelz", 1),)
_DISABLE_CHANNELZ = (("grpc.enable_channelz", 0),)


def _successful_unary_unary(request, servicer_context):
    return _RESPONSE


def _failed_unary_unary(request, servicer_context):
    servicer_context.set_code(grpc.StatusCode.INTERNAL)
    servicer_context.set_details("Channelz Test Intended Failure")


def _successful_stream_stream(request_iterator, servicer_context):
    for _ in request_iterator:
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


class _ChannelServerPair(object):
    def __init__(self):
        # Server will enable channelz service
        self.server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=3),
            options=_DISABLE_REUSE_PORT + _ENABLE_CHANNELZ,
        )
        port = self.server.add_insecure_port("[::]:0")
        self.server.add_generic_rpc_handlers((_GenericHandler(),))
        self.server.start()

        # Channel will enable channelz service...
        self.channel = grpc.insecure_channel(
            "localhost:%d" % port, _ENABLE_CHANNELZ
        )


def _generate_channel_server_pairs(n):
    return [_ChannelServerPair() for i in range(n)]


def _close_channel_server_pairs(pairs):
    for pair in pairs:
        pair.server.stop(None)
        pair.channel.close()


@unittest.skipIf(
    sys.version_info[0] < 3, "ProtoBuf descriptor has moved on from Python2"
)
class ChannelzServicerTest(unittest.TestCase):
    def _send_successful_unary_unary(self, idx):
        _, r = (
            self._pairs[idx]
            .channel.unary_unary(_SUCCESSFUL_UNARY_UNARY)
            .with_call(_REQUEST)
        )
        self.assertEqual(r.code(), grpc.StatusCode.OK)

    def _send_failed_unary_unary(self, idx):
        try:
            self._pairs[idx].channel.unary_unary(_FAILED_UNARY_UNARY).with_call(
                _REQUEST
            )
        except grpc.RpcError:
            return
        else:
            self.fail("This call supposed to fail")

    def _send_successful_stream_stream(self, idx):
        response_iterator = (
            self._pairs[idx]
            .channel.stream_stream(_SUCCESSFUL_STREAM_STREAM)
            .__call__(iter([_REQUEST] * test_constants.STREAM_LENGTH))
        )
        cnt = 0
        for _ in response_iterator:
            cnt += 1
        self.assertEqual(cnt, test_constants.STREAM_LENGTH)

    def _get_channel_id(self, idx):
        """Channel id may not be consecutive"""
        resp = self._channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(start_channel_id=0)
        )
        self.assertGreater(len(resp.channel), idx)
        return resp.channel[idx].ref.channel_id

    def setUp(self):
        self._pairs = []
        # This server is for Channelz info fetching only
        # It self should not enable Channelz
        self._server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=3),
            options=_DISABLE_REUSE_PORT + _DISABLE_CHANNELZ,
        )
        port = self._server.add_insecure_port("[::]:0")
        channelz.add_channelz_servicer(self._server)
        self._server.start()

        # This channel is used to fetch Channelz info only
        # Channelz should not be enabled
        self._channel = grpc.insecure_channel(
            "localhost:%d" % port, _DISABLE_CHANNELZ
        )
        self._channelz_stub = channelz_pb2_grpc.ChannelzStub(self._channel)

    def tearDown(self):
        self._server.stop(None)
        self._channel.close()
        _close_channel_server_pairs(self._pairs)

    def test_get_top_channels_basic(self):
        self._pairs = _generate_channel_server_pairs(1)
        resp = self._channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(start_channel_id=0)
        )
        self.assertEqual(len(resp.channel), 1)
        self.assertEqual(resp.end, True)

    def test_get_top_channels_high_start_id(self):
        self._pairs = _generate_channel_server_pairs(1)
        resp = self._channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(start_channel_id=10000)
        )
        self.assertEqual(len(resp.channel), 0)
        self.assertEqual(resp.end, True)

    def test_successful_request(self):
        self._pairs = _generate_channel_server_pairs(1)
        self._send_successful_unary_unary(0)
        resp = self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=self._get_channel_id(0))
        )
        self.assertEqual(resp.channel.data.calls_started, 1)
        self.assertEqual(resp.channel.data.calls_succeeded, 1)
        self.assertEqual(resp.channel.data.calls_failed, 0)

    def test_failed_request(self):
        self._pairs = _generate_channel_server_pairs(1)
        self._send_failed_unary_unary(0)
        resp = self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=self._get_channel_id(0))
        )
        self.assertEqual(resp.channel.data.calls_started, 1)
        self.assertEqual(resp.channel.data.calls_succeeded, 0)
        self.assertEqual(resp.channel.data.calls_failed, 1)

    def test_many_requests(self):
        self._pairs = _generate_channel_server_pairs(1)
        k_success = 7
        k_failed = 9
        for i in range(k_success):
            self._send_successful_unary_unary(0)
        for i in range(k_failed):
            self._send_failed_unary_unary(0)
        resp = self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=self._get_channel_id(0))
        )
        self.assertEqual(resp.channel.data.calls_started, k_success + k_failed)
        self.assertEqual(resp.channel.data.calls_succeeded, k_success)
        self.assertEqual(resp.channel.data.calls_failed, k_failed)

    def test_many_channel(self):
        k_channels = 4
        self._pairs = _generate_channel_server_pairs(k_channels)
        resp = self._channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(start_channel_id=0)
        )
        self.assertEqual(len(resp.channel), k_channels)

    def test_many_requests_many_channel(self):
        k_channels = 4
        self._pairs = _generate_channel_server_pairs(k_channels)
        k_success = 11
        k_failed = 13
        for i in range(k_success):
            self._send_successful_unary_unary(0)
            self._send_successful_unary_unary(2)
        for i in range(k_failed):
            self._send_failed_unary_unary(1)
            self._send_failed_unary_unary(2)

        # The first channel saw only successes
        resp = self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=self._get_channel_id(0))
        )
        self.assertEqual(resp.channel.data.calls_started, k_success)
        self.assertEqual(resp.channel.data.calls_succeeded, k_success)
        self.assertEqual(resp.channel.data.calls_failed, 0)

        # The second channel saw only failures
        resp = self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=self._get_channel_id(1))
        )
        self.assertEqual(resp.channel.data.calls_started, k_failed)
        self.assertEqual(resp.channel.data.calls_succeeded, 0)
        self.assertEqual(resp.channel.data.calls_failed, k_failed)

        # The third channel saw both successes and failures
        resp = self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=self._get_channel_id(2))
        )
        self.assertEqual(resp.channel.data.calls_started, k_success + k_failed)
        self.assertEqual(resp.channel.data.calls_succeeded, k_success)
        self.assertEqual(resp.channel.data.calls_failed, k_failed)

        # The fourth channel saw nothing
        resp = self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=self._get_channel_id(3))
        )
        self.assertEqual(resp.channel.data.calls_started, 0)
        self.assertEqual(resp.channel.data.calls_succeeded, 0)
        self.assertEqual(resp.channel.data.calls_failed, 0)

    def test_many_subchannels(self):
        k_channels = 4
        self._pairs = _generate_channel_server_pairs(k_channels)
        k_success = 17
        k_failed = 19
        for i in range(k_success):
            self._send_successful_unary_unary(0)
            self._send_successful_unary_unary(2)
        for i in range(k_failed):
            self._send_failed_unary_unary(1)
            self._send_failed_unary_unary(2)

        gtc_resp = self._channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(start_channel_id=0)
        )
        self.assertEqual(len(gtc_resp.channel), k_channels)
        for i in range(k_channels):
            # If no call performed in the channel, there shouldn't be any subchannel
            if gtc_resp.channel[i].data.calls_started == 0:
                self.assertEqual(len(gtc_resp.channel[i].subchannel_ref), 0)
                continue

            # Otherwise, the subchannel should exist
            self.assertGreater(len(gtc_resp.channel[i].subchannel_ref), 0)
            gsc_resp = self._channelz_stub.GetSubchannel(
                channelz_pb2.GetSubchannelRequest(
                    subchannel_id=gtc_resp.channel[i]
                    .subchannel_ref[0]
                    .subchannel_id
                )
            )
            self.assertEqual(
                gtc_resp.channel[i].data.calls_started,
                gsc_resp.subchannel.data.calls_started,
            )
            self.assertEqual(
                gtc_resp.channel[i].data.calls_succeeded,
                gsc_resp.subchannel.data.calls_succeeded,
            )
            self.assertEqual(
                gtc_resp.channel[i].data.calls_failed,
                gsc_resp.subchannel.data.calls_failed,
            )

    def test_server_basic(self):
        self._pairs = _generate_channel_server_pairs(1)
        resp = self._channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0)
        )
        self.assertEqual(len(resp.server), 1)

    def test_get_one_server(self):
        self._pairs = _generate_channel_server_pairs(1)
        gss_resp = self._channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0)
        )
        self.assertEqual(len(gss_resp.server), 1)
        gs_resp = self._channelz_stub.GetServer(
            channelz_pb2.GetServerRequest(
                server_id=gss_resp.server[0].ref.server_id
            )
        )
        self.assertEqual(
            gss_resp.server[0].ref.server_id, gs_resp.server.ref.server_id
        )

    def test_server_call(self):
        self._pairs = _generate_channel_server_pairs(1)
        k_success = 23
        k_failed = 29
        for i in range(k_success):
            self._send_successful_unary_unary(0)
        for i in range(k_failed):
            self._send_failed_unary_unary(0)

        resp = self._channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0)
        )
        self.assertEqual(len(resp.server), 1)
        self.assertEqual(
            resp.server[0].data.calls_started, k_success + k_failed
        )
        self.assertEqual(resp.server[0].data.calls_succeeded, k_success)
        self.assertEqual(resp.server[0].data.calls_failed, k_failed)

    def test_many_subchannels_and_sockets(self):
        k_channels = 4
        self._pairs = _generate_channel_server_pairs(k_channels)
        k_success = 3
        k_failed = 5
        for i in range(k_success):
            self._send_successful_unary_unary(0)
            self._send_successful_unary_unary(2)
        for i in range(k_failed):
            self._send_failed_unary_unary(1)
            self._send_failed_unary_unary(2)

        gtc_resp = self._channelz_stub.GetTopChannels(
            channelz_pb2.GetTopChannelsRequest(start_channel_id=0)
        )
        self.assertEqual(len(gtc_resp.channel), k_channels)
        for i in range(k_channels):
            # If no call performed in the channel, there shouldn't be any subchannel
            if gtc_resp.channel[i].data.calls_started == 0:
                self.assertEqual(len(gtc_resp.channel[i].subchannel_ref), 0)
                continue

            # Otherwise, the subchannel should exist
            self.assertGreater(len(gtc_resp.channel[i].subchannel_ref), 0)
            gsc_resp = self._channelz_stub.GetSubchannel(
                channelz_pb2.GetSubchannelRequest(
                    subchannel_id=gtc_resp.channel[i]
                    .subchannel_ref[0]
                    .subchannel_id
                )
            )
            self.assertEqual(len(gsc_resp.subchannel.socket_ref), 1)

            gs_resp = self._channelz_stub.GetSocket(
                channelz_pb2.GetSocketRequest(
                    socket_id=gsc_resp.subchannel.socket_ref[0].socket_id
                )
            )
            self.assertEqual(
                gsc_resp.subchannel.data.calls_started,
                gs_resp.socket.data.streams_started,
            )
            self.assertEqual(
                gsc_resp.subchannel.data.calls_started,
                gs_resp.socket.data.streams_succeeded,
            )
            # Calls started == messages sent, only valid for unary calls
            self.assertEqual(
                gsc_resp.subchannel.data.calls_started,
                gs_resp.socket.data.messages_sent,
            )
            # Only receive responses when the RPC was successful
            self.assertEqual(
                gsc_resp.subchannel.data.calls_succeeded,
                gs_resp.socket.data.messages_received,
            )

            if gs_resp.socket.remote.HasField("tcpip_address"):
                address = gs_resp.socket.remote.tcpip_address.ip_address
                self.assertTrue(
                    len(address) == 4 or len(address) == 16, address
                )
            if gs_resp.socket.local.HasField("tcpip_address"):
                address = gs_resp.socket.local.tcpip_address.ip_address
                self.assertTrue(
                    len(address) == 4 or len(address) == 16, address
                )

    def test_streaming_rpc(self):
        self._pairs = _generate_channel_server_pairs(1)
        # In C++, the argument for _send_successful_stream_stream is message length.
        # Here the argument is still channel idx, to be consistent with the other two.
        self._send_successful_stream_stream(0)

        gc_resp = self._channelz_stub.GetChannel(
            channelz_pb2.GetChannelRequest(channel_id=self._get_channel_id(0))
        )
        self.assertEqual(gc_resp.channel.data.calls_started, 1)
        self.assertEqual(gc_resp.channel.data.calls_succeeded, 1)
        self.assertEqual(gc_resp.channel.data.calls_failed, 0)
        # Subchannel exists
        self.assertGreater(len(gc_resp.channel.subchannel_ref), 0)

        while True:
            gsc_resp = self._channelz_stub.GetSubchannel(
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
            gs_resp = self._channelz_stub.GetSocket(
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
        self.assertEqual(gs_resp.socket.data.streams_succeeded, 1)
        self.assertEqual(gs_resp.socket.data.streams_failed, 0)
        self.assertEqual(
            gs_resp.socket.data.messages_sent, test_constants.STREAM_LENGTH
        )
        self.assertEqual(
            gs_resp.socket.data.messages_received, test_constants.STREAM_LENGTH
        )

    def test_server_sockets(self):
        self._pairs = _generate_channel_server_pairs(1)
        self._send_successful_unary_unary(0)
        self._send_failed_unary_unary(0)

        gs_resp = self._channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0)
        )
        self.assertEqual(len(gs_resp.server), 1)
        self.assertEqual(gs_resp.server[0].data.calls_started, 2)
        self.assertEqual(gs_resp.server[0].data.calls_succeeded, 1)
        self.assertEqual(gs_resp.server[0].data.calls_failed, 1)

        gss_resp = self._channelz_stub.GetServerSockets(
            channelz_pb2.GetServerSocketsRequest(
                server_id=gs_resp.server[0].ref.server_id, start_socket_id=0
            )
        )
        # If the RPC call failed, it will raise a grpc.RpcError
        # So, if there is no exception raised, considered pass

    def test_server_listen_sockets(self):
        self._pairs = _generate_channel_server_pairs(1)

        gss_resp = self._channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0)
        )
        self.assertEqual(len(gss_resp.server), 1)
        self.assertEqual(len(gss_resp.server[0].listen_socket), 1)

        gs_resp = self._channelz_stub.GetSocket(
            channelz_pb2.GetSocketRequest(
                socket_id=gss_resp.server[0].listen_socket[0].socket_id
            )
        )

        # If the RPC call failed, it will raise a grpc.RpcError
        # So, if there is no exception raised, considered pass

    def test_invalid_query_get_server(self):
        try:
            self._channelz_stub.GetServer(
                channelz_pb2.GetServerRequest(server_id=10000)
            )
        except BaseException as e:
            self.assertIn("StatusCode.NOT_FOUND", str(e))
        else:
            self.fail("Invalid query not detected")

    def test_invalid_query_get_channel(self):
        try:
            self._channelz_stub.GetChannel(
                channelz_pb2.GetChannelRequest(channel_id=10000)
            )
        except BaseException as e:
            self.assertIn("StatusCode.NOT_FOUND", str(e))
        else:
            self.fail("Invalid query not detected")

    def test_invalid_query_get_subchannel(self):
        try:
            self._channelz_stub.GetSubchannel(
                channelz_pb2.GetSubchannelRequest(subchannel_id=10000)
            )
        except BaseException as e:
            self.assertIn("StatusCode.NOT_FOUND", str(e))
        else:
            self.fail("Invalid query not detected")

    def test_invalid_query_get_socket(self):
        try:
            self._channelz_stub.GetSocket(
                channelz_pb2.GetSocketRequest(socket_id=10000)
            )
        except BaseException as e:
            self.assertIn("StatusCode.NOT_FOUND", str(e))
        else:
            self.fail("Invalid query not detected")

    def test_invalid_query_get_server_sockets(self):
        try:
            self._channelz_stub.GetServerSockets(
                channelz_pb2.GetServerSocketsRequest(
                    server_id=10000,
                    start_socket_id=0,
                )
            )
        except BaseException as e:
            self.assertIn("StatusCode.NOT_FOUND", str(e))
        else:
            self.fail("Invalid query not detected")


if __name__ == "__main__":
    unittest.main(verbosity=2)
