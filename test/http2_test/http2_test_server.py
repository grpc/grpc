"""
  HTTP2 Test Server. Highly experimental work in progress.
"""
import struct
import messages_pb2
import argparse
import logging
import time

from twisted.internet.defer import Deferred, inlineCallbacks
from twisted.internet.protocol import Protocol, Factory
from twisted.internet import endpoints, reactor, error, defer
from h2.connection import H2Connection
from h2.events import RequestReceived, DataReceived, WindowUpdated, RemoteSettingsChanged
from threading import Lock
import http2_base_server

READ_CHUNK_SIZE = 16384
GRPC_HEADER_SIZE = 5

class TestcaseRstStreamAfterHeader(object):
  def __init__(self):
    self._base_server = http2_base_server.H2ProtocolBaseServer()
    self._base_server._handlers['RequestReceived'] = self.on_request_received

  def get_base_server(self):
    return self._base_server

  def on_request_received(self, event):
    # send initial headers
    self._base_server.on_request_received_default(event)
    # send reset stream
    self._base_server.send_reset_stream()

class TestcaseRstStreamAfterData(object):
  def __init__(self):
    self._base_server = http2_base_server.H2ProtocolBaseServer()
    self._base_server._handlers['DataReceived'] = self.on_data_received

  def get_base_server(self):
    return self._base_server

  def on_data_received(self, event):
    self._base_server.on_data_received_default(event)
    sr = self._base_server.parse_received_data(self._base_server._recv_buffer)
    assert(sr is not None)
    assert(sr.response_size <= 2048) # so it can fit into one flow control window
    response_data = self._base_server.default_response_data(sr.response_size)
    self._ready_to_send = True
    self._base_server.setup_send(response_data)
    # send reset stream
    self._base_server.send_reset_stream()

class TestcaseGoaway(object):
  """ 
    Process incoming request normally. After sending trailer response,
    send GOAWAY with stream id = 1.
    assert that the next request is made on a different connection.
  """
  def __init__(self, iteration):
    self._base_server = http2_base_server.H2ProtocolBaseServer()
    self._base_server._handlers['RequestReceived'] = self.on_request_received
    self._base_server._handlers['DataReceived'] = self.on_data_received
    self._base_server._handlers['WindowUpdated'] = self.on_window_update_default
    self._base_server._handlers['SendDone'] = self.on_send_done
    self._base_server._handlers['ConnectionLost'] = self.on_connection_lost
    self._ready_to_send = False
    self._iteration = iteration

  def get_base_server(self):
    return self._base_server

  def on_connection_lost(self, reason):
    logging.info('Disconnect received. Count %d'%self._iteration)
    # _iteration == 2 => Two different connections have been used.
    if self._iteration == 2:
      self._base_server.on_connection_lost(reason)

  def on_send_done(self):
    self._base_server.on_send_done_default()
    if self._base_server._stream_id == 1:
      logging.info('Sending GOAWAY for stream 1')
      self._base_server._conn.close_connection(error_code=0, additional_data=None, last_stream_id=1)

  def on_request_received(self, event):
    self._ready_to_send = False
    self._base_server.on_request_received_default(event)

  def on_data_received(self, event):
    self._base_server.on_data_received_default(event)
    sr = self._base_server.parse_received_data(self._base_server._recv_buffer)
    if sr:
      time.sleep(1)
      logging.info('Creating response size = %s'%sr.response_size)
      response_data = self._base_server.default_response_data(sr.response_size)
      self._ready_to_send = True
      self._base_server.setup_send(response_data)

  def on_window_update_default(self, event):
    if self._ready_to_send:
      self._base_server.default_send()

class TestcasePing(object):
  """ 
  """
  def __init__(self, iteration):
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
    sr = self._base_server.parse_received_data(self._base_server._recv_buffer)
    logging.info('Creating response size = %s'%sr.response_size)
    response_data = self._base_server.default_response_data(sr.response_size)
    self._base_server.default_ping()
    self._base_server.setup_send(response_data)
    self._base_server.default_ping()

  def on_connection_lost(self, reason):
    logging.info('Disconnect received. Ping Count %d'%self._base_server._outstanding_pings)
    assert(self._base_server._outstanding_pings == 0)
    self._base_server.on_connection_lost(reason)

class H2Factory(Factory):
  def __init__(self, testcase):
    logging.info('In H2Factory')
    self._num_streams = 0
    self._testcase = testcase

  def buildProtocol(self, addr):
    self._num_streams += 1
    if self._testcase == 'rst_stream_after_header':
      t = TestcaseRstStreamAfterHeader(self._num_streams)
    elif self._testcase == 'rst_stream_after_data':
      t = TestcaseRstStreamAfterData(self._num_streams)
    elif self._testcase == 'goaway':
      t = TestcaseGoaway(self._num_streams)
    elif self._testcase == 'ping':
      t = TestcasePing(self._num_streams)
    else:
      assert(0)
    return t.get_base_server()

if __name__ == "__main__":
  logging.basicConfig(format = "%(levelname) -10s %(asctime)s %(module)s:%(lineno)s | %(message)s", level=logging.INFO)
  parser = argparse.ArgumentParser()
  parser.add_argument("test")
  parser.add_argument("port")
  args = parser.parse_args()
  if args.test not in ['rst_stream_after_header', 'rst_stream_after_data', 'goaway', 'ping']:
    print 'unknown test: ', args.test
  endpoint = endpoints.TCP4ServerEndpoint(reactor, int(args.port), backlog=128)
  endpoint.listen(H2Factory(args.test))
  reactor.run()
