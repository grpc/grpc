# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import hyperframe.frame
import logging

import http2_base_server

class TestcaseSettingsMaxStreams(object):
  """
    This test sets MAX_CONCURRENT_STREAMS to 1 and asserts that at any point
    only 1 stream is active.
  """
  def __init__(self):
    self._base_server = http2_base_server.H2ProtocolBaseServer()
    self._base_server._handlers['DataReceived'] = self.on_data_received
    self._base_server._handlers['ConnectionMade'] = self.on_connection_made

  def get_base_server(self):
    return self._base_server

  def on_connection_made(self):
    logging.info('Connection Made')
    self._base_server._conn.initiate_connection()
    self._base_server._conn.update_settings(
                  {hyperframe.frame.SettingsFrame.MAX_CONCURRENT_STREAMS: 1})
    self._base_server.transport.setTcpNoDelay(True)
    self._base_server.transport.write(self._base_server._conn.data_to_send())

  def on_data_received(self, event):
    self._base_server.on_data_received_default(event)
    sr = self._base_server.parse_received_data(event.stream_id)
    if sr:
      logging.info('Creating response of size = %s' % sr.response_size)
      response_data = self._base_server.default_response_data(sr.response_size)
      self._base_server.setup_send(response_data, event.stream_id)
    # TODO (makdharma): Add assertion to check number of live streams
