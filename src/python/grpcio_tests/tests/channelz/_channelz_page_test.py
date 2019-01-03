# Copyright 2019 The gRPC Authors
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
"""Tests of Serving HTTPS Channelz Page."""

import unittest
import time
import logging

import requests
from pyquery import PyQuery as pq

import grpc
from grpc_channelz.v1 import channelz

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_CHANNELZ_URL_PREFIX_TEMPLATE = 'http://localhost:%d/gdebug/channelz/'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'

_SUCCESSFUL_UNARY_UNARY = '/test/SuccessfulUnaryUnary'
_FAILED_UNARY_UNARY = '/test/FailedUnaryUnary'
_SUCCESSFUL_STREAM_STREAM = '/test/SuccessfulStreamStream'


def _successful_unary_unary(request, servicer_context):
    return _RESPONSE


def _failed_unary_unary(request, servicer_context):
    servicer_context.abort(grpc.StatusCode.INTERNAL, 'Intended Failure')


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
                _successful_stream_stream)
        else:
            return None


class ChannelzPageTest(unittest.TestCase):

    def _send_successful_unary_unary(self):
        _, r = self._channel.unary_unary(_SUCCESSFUL_UNARY_UNARY).with_call(
            _REQUEST)
        self.assertEqual(r.code(), grpc.StatusCode.OK)

    def _send_failed_unary_unary(self):
        try:
            self._channel.unary_unary(_FAILED_UNARY_UNARY)(_REQUEST)
        except grpc.RpcError:
            return

    def _send_successful_stream_stream(self):
        response_iterator = self._channel.stream_stream(
            _SUCCESSFUL_STREAM_STREAM).__call__(
                iter([_REQUEST] * test_constants.STREAM_LENGTH))
        cnt = 0
        for _ in response_iterator:
            cnt += 1
        self.assertEqual(cnt, test_constants.STREAM_LENGTH)

    def setUp(self):
        super(ChannelzPageTest, self).setUp()
        self._server = test_common.test_server()
        port = self._server.add_insecure_port('[::]:0')
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        self._server.start()

        self._channel = grpc.insecure_channel('localhost:%d' % port)

        self._page_server = channelz.serve_channelz_page('', 0)
        self._page_url_prefix = _CHANNELZ_URL_PREFIX_TEMPLATE % self._page_server.server_address[1]

        # Emit RPCs to make sure sockets are created
        self._send_successful_unary_unary()
        self._send_failed_unary_unary()
        self._send_successful_stream_stream()

    def tearDown(self):
        self._page_server.shutdown()
        self._page_server.server_close()
        self._server.stop(None)
        self._channel.close()
        super(ChannelzPageTest, self).tearDown()

    def test_homepage(self):
        resp = requests.get(self._page_url_prefix)
        self.assertEqual(resp.status_code, 200)

    def test_channel(self):
        # Page of list of channels
        resp = requests.get(self._page_url_prefix + 'topchannels')
        self.assertEqual(resp.status_code, 200)

        # Page of detail of a channel
        surffix = pq(resp.text)('table a').attr('href')
        self.assertIn('channel?channel_id=', surffix)
        resp = requests.get(self._page_url_prefix + surffix)
        self.assertEqual(resp.status_code, 200)

        # Page of detail of a subchannel
        surffix = pq(resp.text)('table').eq(1).find('a').attr('href')
        self.assertIn('subchannel?subchannel_id=', surffix)
        resp = requests.get(self._page_url_prefix + surffix)
        self.assertEqual(resp.status_code, 200)

        # Page of detail of a socket
        surffix = pq(resp.text)('table a').attr('href')
        self.assertIn('socket?socket_id=', surffix)
        resp = requests.get(self._page_url_prefix + surffix)
        self.assertEqual(resp.status_code, 200)

    def test_serversockets(self):
        # Page of list of servers
        resp = requests.get(self._page_url_prefix + 'servers')
        self.assertEqual(resp.status_code, 200)

        # TODO(lidiz) In Python 3, the server from last test unit will remain alive...
        # Remove the selection logic when the deallocation is fixed.
        trs = pq(resp.text)('table tr')
        for i in range(len(trs)):
            tds = trs.eq(i).find('td')
            if not tds:
                continue
            if not tds.eq(0).find('a'):
                continue
            if not tds.eq(len(tds) - 1).find('a'):
                continue
            surffix = tds.eq(0).find('a').attr('href')

        # Page of detail of a server
        self.assertIn('serversockets?server_id=', surffix)
        resp = requests.get(self._page_url_prefix + surffix)
        self.assertEqual(resp.status_code, 200)

        # Page of detail of the listen socket
        surffix = pq(resp.text)('table a').attr('href')
        self.assertIn('socket?socket_id=', surffix)
        resp = requests.get(self._page_url_prefix + surffix)
        self.assertEqual(resp.status_code, 200)

    def test_incomplete_arguments(self):
        for surffix in ['channel', 'subchannel', 'socket', 'serversockets']:
            resp = requests.get(self._page_url_prefix + surffix)
            self.assertEqual(resp.status_code, 400)

    def test_not_found_channel(self):
        resp = requests.get(self._page_url_prefix + 'channel?channel_id=999')
        self.assertEqual(resp.status_code, 404)

    def test_not_found_subchannel(self):
        resp = requests.get(
            self._page_url_prefix + 'subchannel?subchannel_id=999')
        self.assertEqual(resp.status_code, 404)

    def test_not_found_socket(self):
        resp = requests.get(self._page_url_prefix + 'socket?socket_id=999')
        self.assertEqual(resp.status_code, 404)

    def test_not_found_serversockets(self):
        resp = requests.get(
            self._page_url_prefix + 'serversockets?server_id=999')
        self.assertEqual(resp.status_code, 404)

    def test_not_found_topchannels(self):
        resp = requests.get(
            self._page_url_prefix + 'topchannels?start_channel_id=999')
        self.assertEqual(resp.status_code, 404)

    def test_not_found_servers(self):
        resp = requests.get(
            self._page_url_prefix + 'servers?start_server_id=999')
        self.assertEqual(resp.status_code, 404)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
