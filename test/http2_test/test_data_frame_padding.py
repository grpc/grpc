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

import http2_base_server
import logging
import messages_pb2

# Set the number of padding bytes per data frame to be very large
# relative to the number of data bytes for each data frame sent.
_LARGE_PADDING_LENGTH = 255
_SMALL_READ_CHUNK_SIZE = 5

class TestDataFramePadding(object):
  """
    In response to an incoming request, this test sends headers, followed by
    data, followed by a reset stream frame. Client asserts that the RPC failed.
    Client needs to deliver the complete message to the application layer.
  """
  def __init__(self, use_padding=True):
    self._base_server = http2_base_server.H2ProtocolBaseServer()
    self._base_server._handlers['DataReceived'] = self.on_data_received
    self._base_server._handlers['WindowUpdated'] = self.on_window_update
    self._base_server._handlers['RequestReceived'] = self.on_request_received

    # _total_updates maps stream ids to total flow control updates received
    self._total_updates = {}
    # zero window updates so far for connection window (stream id '0')
    self._total_updates[0] = 0
    self._read_chunk_size = _SMALL_READ_CHUNK_SIZE

    if use_padding:
      self._pad_length = _LARGE_PADDING_LENGTH
    else:
      self._pad_length = None

  def get_base_server(self):
    return self._base_server

  def on_data_received(self, event):
    logging.info('on data received. Stream id: %d. Data length: %d' % (event.stream_id, len(event.data)))
    self._base_server.on_data_received_default(event)
    if len(event.data) == 0:
      return
    sr = self._base_server.parse_received_data(event.stream_id)
    stream_bytes = ''
    # Check if full grpc msg has been read into the recv buffer yet
    if sr:
      response_data = self._base_server.default_response_data(sr.response_size)
      logging.info('Stream id: %d. total resp size: %d' % (event.stream_id, len(response_data)))
      # Begin sending the response. Add ``self._pad_length`` padding to each
      # data frame and split the whole message into data frames each carrying
      # only self._read_chunk_size of data.
      # The purpose is to have the majority of the data frame response bytes
      # be padding bytes, since ``self._pad_length`` >> ``self._read_chunk_size``.
      self._base_server.setup_send(response_data , event.stream_id, pad_length=self._pad_length, read_chunk_size=self._read_chunk_size)

  def on_request_received(self, event):
    self._base_server.on_request_received_default(event)
    logging.info('on request received. Stream id: %s.' % event.stream_id)
    self._total_updates[event.stream_id] = 0

  # Log debug info and try to resume sending on all currently active streams.
  def on_window_update(self, event):
    logging.info('on window update. Stream id: %s. Delta: %s' % (event.stream_id, event.delta))
    self._total_updates[event.stream_id] += event.delta
    total = self._total_updates[event.stream_id]
    logging.info('... - total updates for stream %d : %d' % (event.stream_id, total))
    self._base_server.on_window_update_default(event, pad_length=self._pad_length, read_chunk_size=self._read_chunk_size)
