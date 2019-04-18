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

class TestcasePing(object):
  """
    This test injects PING frames before and after header and data. Keeps count
    of outstanding ping response and asserts when the count is non-zero at the
    end of the test.
  """
  def __init__(self):
    self._base_server = http2_base_server.H2ProtocolBaseServer()
    self._base_server._handlers['RequestReceived'] = self.on_request_received
    self._base_server._handlers['DataReceived'] = self.on_data_received
    self._base_server._handlers['ConnectionLost'] = self.on_connection_lost

  def get_base_server(self):
    return self._base_server

  def on_request_received(self, event):
    self._base_server.default_ping()
    self._base_server.on_request_received_default(event)
    self._base_server.default_ping()

  def on_data_received(self, event):
    self._base_server.on_data_received_default(event)
    sr = self._base_server.parse_received_data(event.stream_id)
    if sr:
      logging.info('Creating response size = %s' % sr.response_size)
      response_data = self._base_server.default_response_data(sr.response_size)
      self._base_server.default_ping()
      self._base_server.setup_send(response_data, event.stream_id)
      self._base_server.default_ping()

  def on_connection_lost(self, reason):
    logging.info('Disconnect received. Ping Count %d' % self._base_server._outstanding_pings)
    assert(self._base_server._outstanding_pings == 0)
    self._base_server.on_connection_lost(reason)
