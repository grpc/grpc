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
"""Tests of Serving HTML Channelz Page."""

import logging
import re
import threading
import time
import unittest

import grpc
import requests

from grpc_channelz.v1 import channelz
from tests.unit import test_common
from tests.unit.framework.common import test_constants

# Matches first link inside of a table
_table_a_href_re = re.compile(r'<table.+?<a href=[\'"](.+?)[\'"]', re.DOTALL)
# Matches first link inside the second table
_table_eq1_a_href_re = re.compile(r'<table.+?<table.+?<a href=[\'"](.+?)[\'"]',
                                  re.DOTALL)
# Matches all table rows
_tr_re = re.compile(r'<tr>(.+?)</tr>', re.DOTALL)
# Matches first link if there is at least two links
_a_href_a_re = re.compile(r'<a href=[\'"](.+?)[\'"].+?<a', re.DOTALL)

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
        self._page_url_prefix = _CHANNELZ_URL_PREFIX_TEMPLATE % self._page_server.server_address[
            1]
        self._serving_thread = threading.Thread(
            target=self._page_server.serve_forever)
        self._serving_thread.daemon = True
        self._serving_thread.start()

        # Emit RPCs to make sure sockets are created
        self._send_successful_unary_unary()
        self._send_failed_unary_unary()
        self._send_successful_stream_stream()

    def tearDown(self):
        self._page_server.shutdown()
        self._page_server.server_close()
        self._serving_thread.join()
        self._server.stop(None)
        self._channel.close()

    def test_homepage(self):
        resp = requests.get(self._page_url_prefix)
        self.assertEqual(resp.status_code, 200)

    def test_channel(self):
        # Page of list of channels
        resp = requests.get(self._page_url_prefix + 'topchannels')
        self.assertEqual(resp.status_code, 200)

        # Page of detail of a channel
        suffix = _table_a_href_re.search(resp.text).group(1)
        self.assertIn('channel?channel_id=', suffix)
        resp = requests.get(self._page_url_prefix + suffix)
        self.assertEqual(resp.status_code, 200)

        # Page of detail of a subchannel
        suffix = _table_eq1_a_href_re.search(resp.text).group(1)
        self.assertIn('subchannel?subchannel_id=', suffix)
        resp = requests.get(self._page_url_prefix + suffix)
        self.assertEqual(resp.status_code, 200)

        # Page of detail of a socket
        suffix = _table_a_href_re.search(resp.text).group(1)
        self.assertIn('socket?socket_id=', suffix)
        resp = requests.get(self._page_url_prefix + suffix)
        self.assertEqual(resp.status_code, 200)

    def test_serversockets(self):
        # Page of list of servers
        resp = requests.get(self._page_url_prefix + 'servers')
        self.assertEqual(resp.status_code, 200)

        trs = _tr_re.findall(resp.text)
        # There should be 2 table row, one for header, one for alive server.
        self.assertEqual(len(trs), 2)
        groups = [_a_href_a_re.search(content) for content in trs]
        suffix = next(group for group in groups if group is not None).group(1)

        # Page of detail of a server
        self.assertIn('serversockets?server_id=', suffix)
        resp = requests.get(self._page_url_prefix + suffix)
        self.assertEqual(resp.status_code, 200)

        # Page of detail of the server sockets
        suffix = _table_a_href_re.search(resp.text).group(1)
        self.assertIn('socket?socket_id=', suffix)
        resp = requests.get(self._page_url_prefix + suffix)
        self.assertEqual(resp.status_code, 200)

    def test_incomplete_arguments(self):
        for suffix in ['channel', 'subchannel', 'socket', 'serversockets']:
            resp = requests.get(self._page_url_prefix + suffix)
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


class ChannelzPagePreventAbortTest(unittest.TestCase):

    def setUp(self):
        super(ChannelzPagePreventAbortTest, self).setUp()
        self._page_server = channelz.serve_channelz_page('', 0)
        self._page_url_prefix = _CHANNELZ_URL_PREFIX_TEMPLATE % self._page_server.server_address[
            1]
        self._serving_thread = threading.Thread(
            target=self._page_server.serve_forever)
        self._serving_thread.daemon = True
        self._serving_thread.start()

    def tearDown(self):
        self._page_server.shutdown()
        self._page_server.server_close()
        self._serving_thread.join()

    def test_request_page_without_grpc_init(self):
        # This shouldn't trigger SIGABORT
        resp = requests.get(self._page_url_prefix + 'servers?start_server_id=0')
        self.assertEqual(resp.status_code, 503)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
