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
import messages_pb2
import struct

import h2
import h2.connection
import twisted
import twisted.internet
import twisted.internet.protocol

_READ_CHUNK_SIZE = 16384
_GRPC_HEADER_SIZE = 5

class H2ProtocolBaseServer(twisted.internet.protocol.Protocol):
  def __init__(self):
    self._conn = h2.connection.H2Connection(client_side=False)
    self._recv_buffer = {}
    self._handlers = {}
    self._handlers['ConnectionMade'] = self.on_connection_made_default
    self._handlers['DataReceived'] = self.on_data_received_default
    self._handlers['WindowUpdated'] = self.on_window_update_default
    self._handlers['RequestReceived'] = self.on_request_received_default
    self._handlers['SendDone'] = self.on_send_done_default
    self._handlers['ConnectionLost'] = self.on_connection_lost
    self._handlers['PingAcknowledged'] = self.on_ping_acknowledged_default
    self._stream_status = {}
    self._send_remaining = {}
    self._outstanding_pings = 0

  def set_handlers(self, handlers):
    self._handlers = handlers

  def connectionMade(self):
    self._handlers['ConnectionMade']()

  def connectionLost(self, reason):
    self._handlers['ConnectionLost'](reason)

  def on_connection_made_default(self):
    logging.info('Connection Made')
    self._conn.initiate_connection()
    self.transport.setTcpNoDelay(True)
    self.transport.write(self._conn.data_to_send())

  def on_connection_lost(self, reason):
    logging.info('Disconnected %s' % reason)

  def dataReceived(self, data):
    try:
      events = self._conn.receive_data(data)
    except h2.exceptions.ProtocolError:
      # this try/except block catches exceptions due to race between sending
      # GOAWAY and processing a response in flight.
      return
    if self._conn.data_to_send:
      self.transport.write(self._conn.data_to_send())
    for event in events:
      if isinstance(event, h2.events.RequestReceived) and self._handlers.has_key('RequestReceived'):
        logging.info('RequestReceived Event for stream: %d' % event.stream_id)
        self._handlers['RequestReceived'](event)
      elif isinstance(event, h2.events.DataReceived) and self._handlers.has_key('DataReceived'):
        logging.info('DataReceived Event for stream: %d' % event.stream_id)
        self._handlers['DataReceived'](event)
      elif isinstance(event, h2.events.WindowUpdated) and self._handlers.has_key('WindowUpdated'):
        logging.info('WindowUpdated Event for stream: %d' % event.stream_id)
        self._handlers['WindowUpdated'](event)
      elif isinstance(event, h2.events.PingAcknowledged) and self._handlers.has_key('PingAcknowledged'):
        logging.info('PingAcknowledged Event')
        self._handlers['PingAcknowledged'](event)
    self.transport.write(self._conn.data_to_send())

  def on_ping_acknowledged_default(self, event):
    logging.info('ping acknowledged')
    self._outstanding_pings -= 1

  def on_data_received_default(self, event):
    self._conn.acknowledge_received_data(len(event.data), event.stream_id)
    self._recv_buffer[event.stream_id] += event.data

  def on_request_received_default(self, event):
    self._recv_buffer[event.stream_id] = ''
    self._stream_id = event.stream_id
    self._stream_status[event.stream_id] = True
    self._conn.send_headers(
      stream_id=event.stream_id,
      headers=[
          (':status', '200'),
          ('content-type', 'application/grpc'),
          ('grpc-encoding', 'identity'),
          ('grpc-accept-encoding', 'identity,deflate,gzip'),
      ],
    )
    self.transport.write(self._conn.data_to_send())

  def on_window_update_default(self, event):
    # send pending data, if any
    self.default_send(event.stream_id)

  def send_reset_stream(self):
    self._conn.reset_stream(self._stream_id)
    self.transport.write(self._conn.data_to_send())

  def setup_send(self, data_to_send, stream_id):
    logging.info('Setting up data to send for stream_id: %d' % stream_id)
    self._send_remaining[stream_id] = len(data_to_send)
    self._send_offset = 0
    self._data_to_send = data_to_send
    self.default_send(stream_id)

  def default_send(self, stream_id):
    if not self._send_remaining.has_key(stream_id):
      # not setup to send data yet
      return

    while self._send_remaining[stream_id] > 0:
      lfcw = self._conn.local_flow_control_window(stream_id)
      if lfcw == 0:
        break
      chunk_size = min(lfcw, _READ_CHUNK_SIZE)
      bytes_to_send = min(chunk_size, self._send_remaining[stream_id])
      logging.info('flow_control_window = %d. sending [%d:%d] stream_id %d' %
                    (lfcw, self._send_offset, self._send_offset + bytes_to_send,
                    stream_id))
      data = self._data_to_send[self._send_offset : self._send_offset + bytes_to_send]
      try:
        self._conn.send_data(stream_id, data, False)
      except h2.exceptions.ProtocolError:
        logging.info('Stream %d is closed' % stream_id)
        break
      self._send_remaining[stream_id] -= bytes_to_send
      self._send_offset += bytes_to_send
      if self._send_remaining[stream_id] == 0:
        self._handlers['SendDone'](stream_id)

  def default_ping(self):
    logging.info('sending ping')
    self._outstanding_pings += 1
    self._conn.ping(b'\x00'*8)
    self.transport.write(self._conn.data_to_send())

  def on_send_done_default(self, stream_id):
    if self._stream_status[stream_id]:
      self._stream_status[stream_id] = False
      self.default_send_trailer(stream_id)
    else:
      logging.error('Stream %d is already closed' % stream_id)

  def default_send_trailer(self, stream_id):
    logging.info('Sending trailer for stream id %d' % stream_id)
    self._conn.send_headers(stream_id,
      headers=[ ('grpc-status', '0') ],
      end_stream=True
    )
    self.transport.write(self._conn.data_to_send())

  @staticmethod
  def default_response_data(response_size):
    sresp = messages_pb2.SimpleResponse()
    sresp.payload.body = b'\x00'*response_size
    serialized_resp_proto = sresp.SerializeToString()
    response_data = b'\x00' + struct.pack('i', len(serialized_resp_proto))[::-1] + serialized_resp_proto
    return response_data

  def parse_received_data(self, stream_id):
    """ returns a grpc framed string of bytes containing response proto of the size
    asked in request """
    recv_buffer = self._recv_buffer[stream_id]
    grpc_msg_size = struct.unpack('i',recv_buffer[1:5][::-1])[0]
    if len(recv_buffer) != _GRPC_HEADER_SIZE + grpc_msg_size:
      return None
    req_proto_str = recv_buffer[5:5+grpc_msg_size]
    sr = messages_pb2.SimpleRequest()
    sr.ParseFromString(req_proto_str)
    logging.info('Parsed request for stream %d: response_size=%s' % (stream_id, sr.response_size))
    return sr
