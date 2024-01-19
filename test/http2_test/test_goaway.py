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
import time

import http2_base_server


class TestcaseGoaway(object):
    """
    This test does the following:
      Process incoming request normally, i.e. send headers, data and trailers.
      Then send a GOAWAY frame with the stream id of the processed request.
      It checks that the next request is made on a different TCP connection.
    """

    def __init__(self, iteration):
        self._base_server = http2_base_server.H2ProtocolBaseServer()
        self._base_server._handlers[
            "RequestReceived"
        ] = self.on_request_received
        self._base_server._handlers["DataReceived"] = self.on_data_received
        self._base_server._handlers["SendDone"] = self.on_send_done
        self._base_server._handlers["ConnectionLost"] = self.on_connection_lost
        self._ready_to_send = False
        self._iteration = iteration

    def get_base_server(self):
        return self._base_server

    def on_connection_lost(self, reason):
        logging.info("Disconnect received. Count %d" % self._iteration)
        # _iteration == 2 => Two different connections have been used.
        if self._iteration == 2:
            self._base_server.on_connection_lost(reason)

    def on_send_done(self, stream_id):
        self._base_server.on_send_done_default(stream_id)
        logging.info("Sending GOAWAY for stream %d:" % stream_id)
        self._base_server._conn.close_connection(
            error_code=0, additional_data=None, last_stream_id=stream_id
        )
        self._base_server._stream_status[stream_id] = False

    def on_request_received(self, event):
        self._ready_to_send = False
        self._base_server.on_request_received_default(event)

    def on_data_received(self, event):
        self._base_server.on_data_received_default(event)
        sr = self._base_server.parse_received_data(event.stream_id)
        if sr:
            logging.info("Creating response size = %s" % sr.response_size)
            response_data = self._base_server.default_response_data(
                sr.response_size
            )
            self._ready_to_send = True
            self._base_server.setup_send(response_data, event.stream_id)
