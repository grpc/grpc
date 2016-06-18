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

from grpc import _grpcio_metadata
from grpc._adapter import _types
from grpc._adapter import _low
from tests.unit import test_common


def wait_for_events(completion_queues, deadline):
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
    while (self.client_completion_queue.next(float('+inf')).type !=
           _types.EventType.QUEUE_SHUTDOWN):
      pass
    self.server_completion_queue.shutdown()
    while (self.server_completion_queue.next(float('+inf')).type !=
           _types.EventType.QUEUE_SHUTDOWN):
      pass

    del self.client_completion_queue
    del self.server_completion_queue
    del self.server

  def testEcho(self):
    deadline = time.time() + 5
    event_time_tolerance = 2
    deadline_tolerance = 0.25
    client_metadata_ascii_key = 'key'
    client_metadata_ascii_value = 'val'
    client_metadata_bin_key = 'key-bin'
    client_metadata_bin_value = b'\0'*1000
    server_initial_metadata_key = 'init_me_me_me'
    server_initial_metadata_value = 'whodawha?'
    server_trailing_metadata_key = 'california_is_in_a_drought'
    server_trailing_metadata_value = 'zomg it is'
    server_status_code = _types.StatusCode.OK
    server_status_details = 'our work is never over'
    request = 'blarghaflargh'
    response = 'his name is robert paulson'
    method = 'twinkies'
    host = 'hostess'
    server_request_tag = object()
    request_call_result = self.server.request_call(self.server_completion_queue,
                                                   server_request_tag)

    self.assertEqual(_types.CallError.OK, request_call_result)

    client_call_tag = object()
    client_call = self.client_channel.create_call(
        self.client_completion_queue, method, host, deadline)
    client_initial_metadata = [
        (client_metadata_ascii_key, client_metadata_ascii_value),
        (client_metadata_bin_key, client_metadata_bin_value)
    ]
    client_start_batch_result = client_call.start_batch([
        _types.OpArgs.send_initial_metadata(client_initial_metadata),
        _types.OpArgs.send_message(request, 0),
        _types.OpArgs.send_close_from_client(),
        _types.OpArgs.recv_initial_metadata(),
        _types.OpArgs.recv_message(),
        _types.OpArgs.recv_status_on_client()
    ], client_call_tag)
    self.assertEqual(_types.CallError.OK, client_start_batch_result)

    client_no_event, request_event, = wait_for_events(
        [self.client_completion_queue, self.server_completion_queue],
        time.time() + event_time_tolerance)
    self.assertEqual(client_no_event, None)
    self.assertEqual(_types.EventType.OP_COMPLETE, request_event.type)
    self.assertIsInstance(request_event.call, _low.Call)
    self.assertIs(server_request_tag, request_event.tag)
    self.assertEqual(1, len(request_event.results))
    received_initial_metadata = request_event.results[0].initial_metadata
    # Check that our metadata were transmitted
    self.assertTrue(test_common.metadata_transmitted(client_initial_metadata,
                                                     received_initial_metadata))
    # Check that Python's user agent string is a part of the full user agent
    # string
    received_initial_metadata_dict = dict(received_initial_metadata)
    self.assertIn(b'user-agent', received_initial_metadata_dict)
    self.assertIn('Python-gRPC-{}'.format(_grpcio_metadata.__version__).encode(),
                  received_initial_metadata_dict[b'user-agent'])
    self.assertEqual(method.encode(), request_event.call_details.method)
    self.assertEqual(host.encode(), request_event.call_details.host)
    self.assertLess(abs(deadline - request_event.call_details.deadline),
                    deadline_tolerance)

    # Check that the channel is connected, and that both it and the call have
    # the proper target and peer; do this after the first flurry of messages to
    # avoid the possibility that connection was delayed by the core until the
    # first message was sent.
    self.assertEqual(_types.ConnectivityState.READY,
                     self.client_channel.check_connectivity_state(False))
    self.assertIsNotNone(self.client_channel.target())
    self.assertIsNotNone(client_call.peer())

    server_call_tag = object()
    server_call = request_event.call
    server_initial_metadata = [
        (server_initial_metadata_key, server_initial_metadata_value)
    ]
    server_trailing_metadata = [
        (server_trailing_metadata_key, server_trailing_metadata_value)
    ]
    server_start_batch_result = server_call.start_batch([
        _types.OpArgs.send_initial_metadata(server_initial_metadata),
        _types.OpArgs.recv_message(),
        _types.OpArgs.send_message(response, 0),
        _types.OpArgs.recv_close_on_server(),
        _types.OpArgs.send_status_from_server(
            server_trailing_metadata, server_status_code, server_status_details)
    ], server_call_tag)
    self.assertEqual(_types.CallError.OK, server_start_batch_result)

    client_event, server_event, = wait_for_events(
        [self.client_completion_queue, self.server_completion_queue],
        time.time() + event_time_tolerance)

    self.assertEqual(6, len(client_event.results))
    found_client_op_types = set()
    for client_result in client_event.results:
      # we expect each op type to be unique
      self.assertNotIn(client_result.type, found_client_op_types)
      found_client_op_types.add(client_result.type)
      if client_result.type == _types.OpType.RECV_INITIAL_METADATA:
        self.assertTrue(
            test_common.metadata_transmitted(server_initial_metadata,
                                             client_result.initial_metadata))
      elif client_result.type == _types.OpType.RECV_MESSAGE:
        self.assertEqual(response.encode(), client_result.message)
      elif client_result.type == _types.OpType.RECV_STATUS_ON_CLIENT:
        self.assertTrue(
            test_common.metadata_transmitted(server_trailing_metadata,
                                             client_result.trailing_metadata))
        self.assertEqual(server_status_details.encode(), client_result.status.details)
        self.assertEqual(server_status_code, client_result.status.code)
    self.assertEqual(set([
          _types.OpType.SEND_INITIAL_METADATA,
          _types.OpType.SEND_MESSAGE,
          _types.OpType.SEND_CLOSE_FROM_CLIENT,
          _types.OpType.RECV_INITIAL_METADATA,
          _types.OpType.RECV_MESSAGE,
          _types.OpType.RECV_STATUS_ON_CLIENT
      ]), found_client_op_types)

    self.assertEqual(5, len(server_event.results))
    found_server_op_types = set()
    for server_result in server_event.results:
      self.assertNotIn(client_result.type, found_server_op_types)
      found_server_op_types.add(server_result.type)
      if server_result.type == _types.OpType.RECV_MESSAGE:
        self.assertEqual(request.encode(), server_result.message)
      elif server_result.type == _types.OpType.RECV_CLOSE_ON_SERVER:
        self.assertFalse(server_result.cancelled)
    self.assertEqual(set([
          _types.OpType.SEND_INITIAL_METADATA,
          _types.OpType.RECV_MESSAGE,
          _types.OpType.SEND_MESSAGE,
          _types.OpType.RECV_CLOSE_ON_SERVER,
          _types.OpType.SEND_STATUS_FROM_SERVER
      ]), found_server_op_types)

    del client_call
    del server_call


class HangingServerShutdown(unittest.TestCase):

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
    self.server_completion_queue.shutdown()
    while True:
      client_event, server_event = wait_for_events(
          [self.client_completion_queue, self.server_completion_queue],
          float("+inf"))
      if (client_event.type == _types.EventType.QUEUE_SHUTDOWN and
          server_event.type == _types.EventType.QUEUE_SHUTDOWN):
        break

    del self.client_completion_queue
    del self.server_completion_queue
    del self.server

  def testHangingServerCall(self):
    deadline = time.time() + 5
    deadline_tolerance = 0.25
    event_time_tolerance = 2
    cancel_all_calls_time_tolerance = 0.5
    request = 'blarghaflargh'
    method = 'twinkies'
    host = 'hostess'
    server_request_tag = object()
    request_call_result = self.server.request_call(self.server_completion_queue,
                                                   server_request_tag)

    client_call_tag = object()
    client_call = self.client_channel.create_call(self.client_completion_queue,
                                                  method, host, deadline)
    client_start_batch_result = client_call.start_batch([
        _types.OpArgs.send_initial_metadata([]),
        _types.OpArgs.send_message(request, 0),
        _types.OpArgs.send_close_from_client(),
        _types.OpArgs.recv_initial_metadata(),
        _types.OpArgs.recv_message(),
        _types.OpArgs.recv_status_on_client()
    ], client_call_tag)

    client_no_event, request_event, = wait_for_events(
        [self.client_completion_queue, self.server_completion_queue],
        time.time() + event_time_tolerance)

    # Now try to shutdown the server and expect that we see server shutdown
    # almost immediately after calling cancel_all_calls.

    # First attempt to cancel all calls before shutting down, and expect
    # our state machine to catch the erroneous API use.
    with self.assertRaises(RuntimeError):
      self.server.cancel_all_calls()

    shutdown_tag = object()
    self.server.shutdown(shutdown_tag)
    pre_cancel_timestamp = time.time()
    self.server.cancel_all_calls()
    finish_shutdown_timestamp = None
    client_call_event, server_shutdown_event = wait_for_events(
        [self.client_completion_queue, self.server_completion_queue],
        time.time() + event_time_tolerance)
    self.assertIs(shutdown_tag, server_shutdown_event.tag)
    self.assertGreater(pre_cancel_timestamp + cancel_all_calls_time_tolerance,
                       time.time())

    del client_call


if __name__ == '__main__':
  unittest.main(verbosity=2)
