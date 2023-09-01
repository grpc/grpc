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

import logging

import http2_base_server
import hyperframe.frame


class TestcaseSettingsMaxStreams(object):
    """
    This test sets MAX_CONCURRENT_STREAMS to 1 and asserts that at any point
    only 1 stream is active.
    """

    def __init__(self):
        self._base_server = http2_base_server.H2ProtocolBaseServer()
        self._base_server._handlers["DataReceived"] = self.on_data_received
        self._base_server._handlers["ConnectionMade"] = self.on_connection_made

    def get_base_server(self):
        return self._base_server

    def on_connection_made(self):
        logging.info("Connection Made")
        self._base_server._conn.initiate_connection()
        self._base_server._conn.update_settings(
            {hyperframe.frame.SettingsFrame.MAX_CONCURRENT_STREAMS: 1}
        )
        self._base_server.transport.setTcpNoDelay(True)
        self._base_server.transport.write(
            self._base_server._conn.data_to_send()
        )

    def on_data_received(self, event):
        self._base_server.on_data_received_default(event)
        sr = self._base_server.parse_received_data(event.stream_id)
        if sr:
            logging.info("Creating response of size = %s" % sr.response_size)
            response_data = self._base_server.default_response_data(
                sr.response_size
            )
            self._base_server.setup_send(response_data, event.stream_id)
        # TODO (makdharma): Add assertion to check number of live streams
