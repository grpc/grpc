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
    self._base_server._handlers['RequestReceived'] = self.on_request_received
    self._base_server._handlers['DataReceived'] = self.on_data_received
    self._base_server._handlers['SendDone'] = self.on_send_done
    self._base_server._handlers['ConnectionLost'] = self.on_connection_lost
    self._ready_to_send = False
    self._iteration = iteration

  def get_base_server(self):
    return self._base_server

  def on_connection_lost(self, reason):
    logging.info('Disconnect received. Count %d' % self._iteration)
    # _iteration == 2 => Two different connections have been used.
    if self._iteration == 2:
      self._base_server.on_connection_lost(reason)

  def on_send_done(self, stream_id):
    self._base_server.on_send_done_default(stream_id)
    logging.info('Sending GOAWAY for stream %d:' % stream_id)
    self._base_server._conn.close_connection(error_code=0, additional_data=None, last_stream_id=stream_id)
    self._base_server._stream_status[stream_id] = False

  def on_request_received(self, event):
    self._ready_to_send = False
    self._base_server.on_request_received_default(event)

  def on_data_received(self, event):
    self._base_server.on_data_received_default(event)
    sr = self._base_server.parse_received_data(event.stream_id)
    if sr:
      logging.info('Creating response size = %s' % sr.response_size)
      response_data = self._base_server.default_response_data(sr.response_size)
      self._ready_to_send = True
      self._base_server.setup_send(response_data, event.stream_id)
