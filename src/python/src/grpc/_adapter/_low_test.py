# Copyright 2015, Google Inc.
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

import threading
import time
import unittest

from grpc._adapter import _types
from grpc._adapter import _low


def WaitForEvents(completion_queues, deadline):
  """
  Args:
    completion_queues: list of completion queues to wait for events on
    deadline: absolute deadline to wait until

  Returns:
    a sequence of events of length len(completion_queues).
  """

  results = [None] * len(completion_queues)
  lock = threading.Lock()
  threads = []
  def set_ith_result(i, completion_queue):
    result = completion_queue.next(deadline)
    with lock:
      print i, completion_queue, result, time.time() - deadline
      results[i] = result
  for i, completion_queue in enumerate(completion_queues):
    thread = threading.Thread(target=set_ith_result,
                              args=[i, completion_queue])
    thread.start()
    threads.append(thread)
  for thread in threads:
    thread.join()
  return results

class InsecureServerInsecureClient(unittest.TestCase):

  def setUp(self):
    self.server_completion_queue = _low.CompletionQueue()
    self.server = _low.Server(self.server_completion_queue, [])
    self.port = self.server.add_http2_port('[::]:0')
    self.client_completion_queue = _low.CompletionQueue()
    self.client_channel = _low.Channel('localhost:%d'%self.port, [])

    self.server.start()

  def tearDown(self):
    self.server.shutdown()
    del self.client_channel

    self.client_completion_queue.shutdown()
    while self.client_completion_queue.next().type != _types.EventType.QUEUE_SHUTDOWN:
      pass
    self.server_completion_queue.shutdown()
    while self.server_completion_queue.next().type != _types.EventType.QUEUE_SHUTDOWN:
      pass

    del self.client_completion_queue
    del self.server_completion_queue
    del self.server

  def testEcho(self):
    DEADLINE = time.time()+5
    DEADLINE_TOLERANCE = 0.25
    CLIENT_METADATA_ASCII_KEY = 'key'
    CLIENT_METADATA_ASCII_VALUE = 'val'
    CLIENT_METADATA_BIN_KEY = 'key-bin'
    CLIENT_METADATA_BIN_VALUE = b'\0'*1000
    SERVER_INITIAL_METADATA_KEY = 'init_me_me_me'
    SERVER_INITIAL_METADATA_VALUE = 'whodawha?'
    SERVER_TRAILING_METADATA_KEY = 'California_is_in_a_drought'
    SERVER_TRAILING_METADATA_VALUE = 'zomg it is'
    SERVER_STATUS_CODE = _types.StatusCode.OK
    SERVER_STATUS_DETAILS = 'our work is never over'
    REQUEST = 'in death a member of project mayhem has a name'
    RESPONSE = 'his name is robert paulson'
    METHOD = 'twinkies'
    HOST = 'hostess'
    server_request_tag = object()
    request_call_result = self.server.request_call(self.server_completion_queue, server_request_tag)

    self.assertEquals(_types.CallError.OK, request_call_result)

    client_call_tag = object()
    client_call = self.client_channel.create_call(self.client_completion_queue, METHOD, HOST, DEADLINE)
    client_initial_metadata = [(CLIENT_METADATA_ASCII_KEY, CLIENT_METADATA_ASCII_VALUE), (CLIENT_METADATA_BIN_KEY, CLIENT_METADATA_BIN_VALUE)]
    client_start_batch_result = client_call.start_batch([
        _types.OpArgs.send_initial_metadata(client_initial_metadata),
        _types.OpArgs.send_message(REQUEST),
        _types.OpArgs.send_close_from_client(),
        _types.OpArgs.recv_initial_metadata(),
        _types.OpArgs.recv_message(),
        _types.OpArgs.recv_status_on_client()
    ], client_call_tag)
    self.assertEquals(_types.CallError.OK, client_start_batch_result)

    client_no_event, request_event, = WaitForEvents([self.client_completion_queue, self.server_completion_queue], time.time() + 2)
    self.assertEquals(client_no_event, None)
    self.assertEquals(_types.EventType.OP_COMPLETE, request_event.type)
    self.assertIsInstance(request_event.call, _low.Call)
    self.assertIs(server_request_tag, request_event.tag)
    self.assertEquals(1, len(request_event.results))
    got_initial_metadata = dict(request_event.results[0].initial_metadata)
    self.assertEquals(
        dict(client_initial_metadata),
        dict((x, got_initial_metadata[x]) for x in zip(*client_initial_metadata)[0]))
    self.assertEquals(METHOD, request_event.call_details.method)
    self.assertEquals(HOST, request_event.call_details.host)
    self.assertLess(abs(DEADLINE - request_event.call_details.deadline), DEADLINE_TOLERANCE)

    server_call_tag = object()
    server_call = request_event.call
    server_initial_metadata = [(SERVER_INITIAL_METADATA_KEY, SERVER_INITIAL_METADATA_VALUE)]
    server_trailing_metadata = [(SERVER_TRAILING_METADATA_KEY, SERVER_TRAILING_METADATA_VALUE)]
    server_start_batch_result = server_call.start_batch([
        _types.OpArgs.send_initial_metadata(server_initial_metadata),
        _types.OpArgs.recv_message(),
        _types.OpArgs.send_message(RESPONSE),
        _types.OpArgs.recv_close_on_server(),
        _types.OpArgs.send_status_from_server(server_trailing_metadata, SERVER_STATUS_CODE, SERVER_STATUS_DETAILS)
    ], server_call_tag)
    self.assertEquals(_types.CallError.OK, server_start_batch_result)

    client_event, server_event, = WaitForEvents([self.client_completion_queue, self.server_completion_queue], time.time() + 1)

    self.assertEquals(6, len(client_event.results))
    found_client_op_types = set()
    for client_result in client_event.results:
      self.assertNotIn(client_result.type, found_client_op_types)  # we expect each op type to be unique
      found_client_op_types.add(client_result.type)
      if client_result.type == _types.OpType.RECV_INITIAL_METADATA:
        self.assertEquals(dict(server_initial_metadata), dict(client_result.initial_metadata))
      elif client_result.type == _types.OpType.RECV_MESSAGE:
        self.assertEquals(RESPONSE, client_result.message)
      elif client_result.type == _types.OpType.RECV_STATUS_ON_CLIENT:
        self.assertEquals(dict(server_trailing_metadata), dict(client_result.trailing_metadata))
        self.assertEquals(SERVER_STATUS_DETAILS, client_result.status.details)
        self.assertEquals(SERVER_STATUS_CODE, client_result.status.code)
    self.assertEquals(set([
          _types.OpType.SEND_INITIAL_METADATA,
          _types.OpType.SEND_MESSAGE,
          _types.OpType.SEND_CLOSE_FROM_CLIENT,
          _types.OpType.RECV_INITIAL_METADATA,
          _types.OpType.RECV_MESSAGE,
          _types.OpType.RECV_STATUS_ON_CLIENT
      ]), found_client_op_types)

    self.assertEquals(5, len(server_event.results))
    found_server_op_types = set()
    for server_result in server_event.results:
      self.assertNotIn(client_result.type, found_server_op_types)
      found_server_op_types.add(server_result.type)
      if server_result.type == _types.OpType.RECV_MESSAGE:
        self.assertEquals(REQUEST, server_result.message)
      elif server_result.type == _types.OpType.RECV_CLOSE_ON_SERVER:
        self.assertFalse(server_result.cancelled)
    self.assertEquals(set([
          _types.OpType.SEND_INITIAL_METADATA,
          _types.OpType.RECV_MESSAGE,
          _types.OpType.SEND_MESSAGE,
          _types.OpType.RECV_CLOSE_ON_SERVER,
          _types.OpType.SEND_STATUS_FROM_SERVER
      ]), found_server_op_types)

    del client_call
    del server_call


if __name__ == '__main__':
  unittest.main(verbosity=2)
