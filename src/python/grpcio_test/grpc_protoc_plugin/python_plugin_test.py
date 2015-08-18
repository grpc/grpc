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

import argparse
import contextlib
import distutils.spawn
import errno
import itertools
import os
import pkg_resources
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from grpc.framework.alpha import exceptions
from grpc.framework.foundation import future

# Identifiers of entities we expect to find in the generated module.
SERVICER_IDENTIFIER = 'EarlyAdopterTestServiceServicer'
SERVER_IDENTIFIER = 'EarlyAdopterTestServiceServer'
STUB_IDENTIFIER = 'EarlyAdopterTestServiceStub'
SERVER_FACTORY_IDENTIFIER = 'early_adopter_create_TestService_server'
STUB_FACTORY_IDENTIFIER = 'early_adopter_create_TestService_stub'

# The timeout used in tests of RPCs that are supposed to expire.
SHORT_TIMEOUT = 2
# The timeout used in tests of RPCs that are not supposed to expire. The
# absurdly large value doesn't matter since no passing execution of this test
# module will ever wait the duration.
LONG_TIMEOUT = 600
NO_DELAY = 0


class _ServicerMethods(object):

  def __init__(self, test_pb2, delay):
    self._condition = threading.Condition()
    self._delay = delay
    self._paused = False
    self._fail = False
    self._test_pb2 = test_pb2

  @contextlib.contextmanager
  def pause(self):  # pylint: disable=invalid-name
    with self._condition:
      self._paused = True
    yield
    with self._condition:
      self._paused = False
      self._condition.notify_all()

  @contextlib.contextmanager
  def fail(self):  # pylint: disable=invalid-name
    with self._condition:
      self._fail = True
    yield
    with self._condition:
      self._fail = False

  def _control(self):  # pylint: disable=invalid-name
    with self._condition:
      if self._fail:
        raise ValueError()
      while self._paused:
        self._condition.wait()
    time.sleep(self._delay)

  def UnaryCall(self, request, unused_rpc_context):
    response = self._test_pb2.SimpleResponse()
    response.payload.payload_type = self._test_pb2.COMPRESSABLE
    response.payload.payload_compressable = 'a' * request.response_size
    self._control()
    return response

  def StreamingOutputCall(self, request, unused_rpc_context):
    for parameter in request.response_parameters:
      response = self._test_pb2.StreamingOutputCallResponse()
      response.payload.payload_type = self._test_pb2.COMPRESSABLE
      response.payload.payload_compressable = 'a' * parameter.size
      self._control()
      yield response

  def StreamingInputCall(self, request_iter, unused_rpc_context):
    response = self._test_pb2.StreamingInputCallResponse()
    aggregated_payload_size = 0
    for request in request_iter:
      aggregated_payload_size += len(request.payload.payload_compressable)
    response.aggregated_payload_size = aggregated_payload_size
    self._control()
    return response

  def FullDuplexCall(self, request_iter, unused_rpc_context):
    for request in request_iter:
      for parameter in request.response_parameters:
        response = self._test_pb2.StreamingOutputCallResponse()
        response.payload.payload_type = self._test_pb2.COMPRESSABLE
        response.payload.payload_compressable = 'a' * parameter.size
        self._control()
        yield response

  def HalfDuplexCall(self, request_iter, unused_rpc_context):
    responses = []
    for request in request_iter:
      for parameter in request.response_parameters:
        response = self._test_pb2.StreamingOutputCallResponse()
        response.payload.payload_type = self._test_pb2.COMPRESSABLE
        response.payload.payload_compressable = 'a' * parameter.size
        self._control()
        responses.append(response)
    for response in responses:
      yield response


@contextlib.contextmanager
def _CreateService(test_pb2, delay):
  """Provides a servicer backend and a stub.

  The servicer is just the implementation
  of the actual servicer passed to the face player of the python RPC
  implementation; the two are detached.

  Non-zero delay puts a delay on each call to the servicer, representative of
  communication latency. Timeout is the default timeout for the stub while
  waiting for the service.

  Args:
    test_pb2: The test_pb2 module generated by this test.
    delay: Delay in seconds per response from the servicer.

  Yields:
    A (servicer_methods, servicer, stub) three-tuple where servicer_methods is
      the back-end of the service bound to the stub and the server and stub
      are both activated and ready for use.
  """
  servicer_methods = _ServicerMethods(test_pb2, delay)

  class Servicer(getattr(test_pb2, SERVICER_IDENTIFIER)):

    def UnaryCall(self, request, context):
      return servicer_methods.UnaryCall(request, context)

    def StreamingOutputCall(self, request, context):
      return servicer_methods.StreamingOutputCall(request, context)

    def StreamingInputCall(self, request_iter, context):
      return servicer_methods.StreamingInputCall(request_iter, context)

    def FullDuplexCall(self, request_iter, context):
      return servicer_methods.FullDuplexCall(request_iter, context)

    def HalfDuplexCall(self, request_iter, context):
      return servicer_methods.HalfDuplexCall(request_iter, context)

  servicer = Servicer()
  server = getattr(
      test_pb2, SERVER_FACTORY_IDENTIFIER)(servicer, 0)
  with server:
    port = server.port()
    stub = getattr(test_pb2, STUB_FACTORY_IDENTIFIER)('localhost', port)
    with stub:
      yield servicer_methods, stub, server


def _streaming_input_request_iterator(test_pb2):
  for _ in range(3):
    request = test_pb2.StreamingInputCallRequest()
    request.payload.payload_type = test_pb2.COMPRESSABLE
    request.payload.payload_compressable = 'a'
    yield request


def _streaming_output_request(test_pb2):
  request = test_pb2.StreamingOutputCallRequest()
  sizes = [1, 2, 3]
  request.response_parameters.add(size=sizes[0], interval_us=0)
  request.response_parameters.add(size=sizes[1], interval_us=0)
  request.response_parameters.add(size=sizes[2], interval_us=0)
  return request


def _full_duplex_request_iterator(test_pb2):
  request = test_pb2.StreamingOutputCallRequest()
  request.response_parameters.add(size=1, interval_us=0)
  yield request
  request = test_pb2.StreamingOutputCallRequest()
  request.response_parameters.add(size=2, interval_us=0)
  request.response_parameters.add(size=3, interval_us=0)
  yield request


class PythonPluginTest(unittest.TestCase):
  """Test case for the gRPC Python protoc-plugin.

  While reading these tests, remember that the futures API
  (`stub.method.async()`) only gives futures for the *non-streaming* responses,
  else it behaves like its blocking cousin.
  """

  def setUp(self):
    # Assume that the appropriate protoc and grpc_python_plugins are on the
    # path.
    protoc_command = 'protoc'
    protoc_plugin_filename = distutils.spawn.find_executable(
        'grpc_python_plugin')
    test_proto_filename = pkg_resources.resource_filename(
        'grpc_protoc_plugin', 'test.proto')
    if not os.path.isfile(protoc_command):
      # Assume that if we haven't built protoc that it's on the system.
      protoc_command = 'protoc'

    # Ensure that the output directory exists.
    self.outdir = tempfile.mkdtemp()

    # Invoke protoc with the plugin.
    cmd = [
        protoc_command,
        '--plugin=protoc-gen-python-grpc=%s' % protoc_plugin_filename,
        '-I .',
        '--python_out=%s' % self.outdir,
        '--python-grpc_out=%s' % self.outdir,
        os.path.basename(test_proto_filename),
    ]
    subprocess.check_call(' '.join(cmd), shell=True, env=os.environ,
                          cwd=os.path.dirname(test_proto_filename))
    sys.path.append(self.outdir)

  def tearDown(self):
    try:
      shutil.rmtree(self.outdir)
    except OSError as exc:
      if exc.errno != errno.ENOENT:
        raise

  # TODO(atash): Figure out which of these tests is hanging flakily with small
  # probability.

  def testImportAttributes(self):
    # check that we can access the generated module and its members.
    import test_pb2  # pylint: disable=g-import-not-at-top
    self.assertIsNotNone(getattr(test_pb2, SERVICER_IDENTIFIER, None))
    self.assertIsNotNone(getattr(test_pb2, SERVER_IDENTIFIER, None))
    self.assertIsNotNone(getattr(test_pb2, STUB_IDENTIFIER, None))
    self.assertIsNotNone(getattr(test_pb2, SERVER_FACTORY_IDENTIFIER, None))
    self.assertIsNotNone(getattr(test_pb2, STUB_FACTORY_IDENTIFIER, None))

  def testUpDown(self):
    import test_pb2
    with _CreateService(
        test_pb2, NO_DELAY) as (servicer, stub, unused_server):
      request = test_pb2.SimpleRequest(response_size=13)

  def testUnaryCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (methods, stub, unused_server):
      timeout = 6  # TODO(issue 2039): LONG_TIMEOUT like the other methods.
      request = test_pb2.SimpleRequest(response_size=13)
      response = stub.UnaryCall(request, timeout)
    expected_response = methods.UnaryCall(request, 'not a real RpcContext!')
    self.assertEqual(expected_response, response)

  def testUnaryCallAsync(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = test_pb2.SimpleRequest(response_size=13)
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      # Check that the call does not block waiting for the server to respond.
      with methods.pause():
        response_future = stub.UnaryCall.async(request, LONG_TIMEOUT)
      response = response_future.result()
    expected_response = methods.UnaryCall(request, 'not a real RpcContext!')
    self.assertEqual(expected_response, response)

  def testUnaryCallAsyncExpired(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      request = test_pb2.SimpleRequest(response_size=13)
      with methods.pause():
        response_future = stub.UnaryCall.async(request, SHORT_TIMEOUT)
        with self.assertRaises(exceptions.ExpirationError):
          response_future.result()

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testUnaryCallAsyncCancelled(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = test_pb2.SimpleRequest(response_size=13)
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.pause():
        response_future = stub.UnaryCall.async(request, 1)
        response_future.cancel()
        self.assertTrue(response_future.cancelled())

  def testUnaryCallAsyncFailed(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = test_pb2.SimpleRequest(response_size=13)
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.fail():
        response_future = stub.UnaryCall.async(request, LONG_TIMEOUT)
        self.assertIsNotNone(response_future.exception())

  def testStreamingOutputCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = _streaming_output_request(test_pb2)
    with _CreateService(test_pb2, NO_DELAY) as (methods, stub, unused_server):
      responses = stub.StreamingOutputCall(request, LONG_TIMEOUT)
      expected_responses = methods.StreamingOutputCall(
          request, 'not a real RpcContext!')
      for expected_response, response in itertools.izip_longest(
          expected_responses, responses):
        self.assertEqual(expected_response, response)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testStreamingOutputCallExpired(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = _streaming_output_request(test_pb2)
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.pause():
        responses = stub.StreamingOutputCall(request, SHORT_TIMEOUT)
        with self.assertRaises(exceptions.ExpirationError):
          list(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testStreamingOutputCallCancelled(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = _streaming_output_request(test_pb2)
    with _CreateService(test_pb2, NO_DELAY) as (
        unused_methods, stub, unused_server):
      responses = stub.StreamingOutputCall(request, SHORT_TIMEOUT)
      next(responses)
      responses.cancel()
      with self.assertRaises(future.CancelledError):
        next(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this times out '
                 'instead of raising the proper error.')
  def testStreamingOutputCallFailed(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = _streaming_output_request(test_pb2)
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.fail():
        responses = stub.StreamingOutputCall(request, 1)
        self.assertIsNotNone(responses)
        with self.assertRaises(exceptions.ServicerError):
          next(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testStreamingInputCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (methods, stub, unused_server):
      response = stub.StreamingInputCall(
          _streaming_input_request_iterator(test_pb2), LONG_TIMEOUT)
    expected_response = methods.StreamingInputCall(
        _streaming_input_request_iterator(test_pb2), 'not a real RpcContext!')
    self.assertEqual(expected_response, response)

  def testStreamingInputCallAsync(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.pause():
        response_future = stub.StreamingInputCall.async(
            _streaming_input_request_iterator(test_pb2), LONG_TIMEOUT)
      response = response_future.result()
    expected_response = methods.StreamingInputCall(
        _streaming_input_request_iterator(test_pb2), 'not a real RpcContext!')
    self.assertEqual(expected_response, response)

  def testStreamingInputCallAsyncExpired(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.pause():
        response_future = stub.StreamingInputCall.async(
            _streaming_input_request_iterator(test_pb2), SHORT_TIMEOUT)
        with self.assertRaises(exceptions.ExpirationError):
          response_future.result()
        self.assertIsInstance(
            response_future.exception(), exceptions.ExpirationError)

  def testStreamingInputCallAsyncCancelled(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.pause():
        timeout = 6  # TODO(issue 2039): LONG_TIMEOUT like the other methods.
        response_future = stub.StreamingInputCall.async(
            _streaming_input_request_iterator(test_pb2), timeout)
        response_future.cancel()
        self.assertTrue(response_future.cancelled())
      with self.assertRaises(future.CancelledError):
        response_future.result()

  def testStreamingInputCallAsyncFailed(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.fail():
        response_future = stub.StreamingInputCall.async(
            _streaming_input_request_iterator(test_pb2), SHORT_TIMEOUT)
        self.assertIsNotNone(response_future.exception())

  def testFullDuplexCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (methods, stub, unused_server):
      responses = stub.FullDuplexCall(
          _full_duplex_request_iterator(test_pb2), LONG_TIMEOUT)
      expected_responses = methods.FullDuplexCall(
          _full_duplex_request_iterator(test_pb2), 'not a real RpcContext!')
      for expected_response, response in itertools.izip_longest(
          expected_responses, responses):
        self.assertEqual(expected_response, response)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testFullDuplexCallExpired(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request_iterator = _full_duplex_request_iterator(test_pb2)
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.pause():
        responses = stub.FullDuplexCall(request_iterator, SHORT_TIMEOUT)
        with self.assertRaises(exceptions.ExpirationError):
          list(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testFullDuplexCallCancelled(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (methods, stub, unused_server):
      request_iterator = _full_duplex_request_iterator(test_pb2)
      responses = stub.FullDuplexCall(request_iterator, LONG_TIMEOUT)
      next(responses)
      responses.cancel()
      with self.assertRaises(future.CancelledError):
        next(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this hangs forever '
                 'and fix.')
  def testFullDuplexCallFailed(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request_iterator = _full_duplex_request_iterator(test_pb2)
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      with methods.fail():
        responses = stub.FullDuplexCall(request_iterator, LONG_TIMEOUT)
        self.assertIsNotNone(responses)
        with self.assertRaises(exceptions.ServicerError):
          next(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testHalfDuplexCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (
        methods, stub, unused_server):
      def half_duplex_request_iterator():
        request = test_pb2.StreamingOutputCallRequest()
        request.response_parameters.add(size=1, interval_us=0)
        yield request
        request = test_pb2.StreamingOutputCallRequest()
        request.response_parameters.add(size=2, interval_us=0)
        request.response_parameters.add(size=3, interval_us=0)
        yield request
      responses = stub.HalfDuplexCall(
          half_duplex_request_iterator(), LONG_TIMEOUT)
      expected_responses = methods.HalfDuplexCall(
          half_duplex_request_iterator(), 'not a real RpcContext!')
      for check in itertools.izip_longest(expected_responses, responses):
        expected_response, response = check
        self.assertEqual(expected_response, response)

  def testHalfDuplexCallWedged(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    condition = threading.Condition()
    wait_cell = [False]
    @contextlib.contextmanager
    def wait():  # pylint: disable=invalid-name
      # Where's Python 3's 'nonlocal' statement when you need it?
      with condition:
        wait_cell[0] = True
      yield
      with condition:
        wait_cell[0] = False
        condition.notify_all()
    def half_duplex_request_iterator():
      request = test_pb2.StreamingOutputCallRequest()
      request.response_parameters.add(size=1, interval_us=0)
      yield request
      with condition:
        while wait_cell[0]:
          condition.wait()
    with _CreateService(test_pb2, NO_DELAY) as (methods, stub, unused_server):
      with wait():
        responses = stub.HalfDuplexCall(
            half_duplex_request_iterator(), SHORT_TIMEOUT)
        # half-duplex waits for the client to send all info
        with self.assertRaises(exceptions.ExpirationError):
          next(responses)


if __name__ == '__main__':
  os.chdir(os.path.dirname(sys.argv[0]))
  unittest.main(verbosity=2)
