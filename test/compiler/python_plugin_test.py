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
import errno
import itertools
import os
import shutil
import subprocess
import sys
import tempfile
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

# Timeouts and delays.
SHORT_TIMEOUT = 0.1
NORMAL_TIMEOUT = 1
LONG_TIMEOUT = 2
DOES_NOT_MATTER_DELAY = 0
NO_DELAY = 0
LONG_DELAY = 1

# Build mode environment variable set by tools/run_tests/run_tests.py.
_build_mode = os.environ['CONFIG']


class _ServicerMethods(object):

  def __init__(self, test_pb2, delay):
    self._paused = False
    self._failed = False
    self.test_pb2 = test_pb2
    self.delay = delay

  @contextlib.contextmanager
  def pause(self):  # pylint: disable=invalid-name
    self._paused = True
    yield
    self._paused = False

  @contextlib.contextmanager
  def fail(self):  # pylint: disable=invalid-name
    self._failed = True
    yield
    self._failed = False

  def _control(self):  # pylint: disable=invalid-name
    if self._failed:
      raise ValueError()
    time.sleep(self.delay)
    while self._paused:
      time.sleep(0)

  def UnaryCall(self, request, unused_context):
    response = self.test_pb2.SimpleResponse()
    response.payload.payload_type = self.test_pb2.COMPRESSABLE
    response.payload.payload_compressable = 'a' * request.response_size
    self._control()
    return response

  def StreamingOutputCall(self, request, unused_context):
    for parameter in request.response_parameters:
      response = self.test_pb2.StreamingOutputCallResponse()
      response.payload.payload_type = self.test_pb2.COMPRESSABLE
      response.payload.payload_compressable = 'a' * parameter.size
      self._control()
      yield response

  def StreamingInputCall(self, request_iter, unused_context):
    response = self.test_pb2.StreamingInputCallResponse()
    aggregated_payload_size = 0
    for request in request_iter:
      aggregated_payload_size += len(request.payload.payload_compressable)
    response.aggregated_payload_size = aggregated_payload_size
    self._control()
    return response

  def FullDuplexCall(self, request_iter, unused_context):
    for request in request_iter:
      for parameter in request.response_parameters:
        response = self.test_pb2.StreamingOutputCallResponse()
        response.payload.payload_type = self.test_pb2.COMPRESSABLE
        response.payload.payload_compressable = 'a' * parameter.size
        self._control()
        yield response

  def HalfDuplexCall(self, request_iter, unused_context):
    responses = []
    for request in request_iter:
      for parameter in request.response_parameters:
        response = self.test_pb2.StreamingOutputCallResponse()
        response.payload.payload_type = self.test_pb2.COMPRESSABLE
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
    test_pb2: the test_pb2 module generated by this test
    delay: delay in seconds per response from the servicer
    timeout: how long the stub will wait for the servicer by default.

  Yields:
    A three-tuple (servicer_methods, servicer, stub), where the servicer is
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


def StreamingInputRequest(test_pb2):
  for _ in range(3):
    request = test_pb2.StreamingInputCallRequest()
    request.payload.payload_type = test_pb2.COMPRESSABLE
    request.payload.payload_compressable = 'a'
    yield request


def StreamingOutputRequest(test_pb2):
  request = test_pb2.StreamingOutputCallRequest()
  sizes = [1, 2, 3]
  request.response_parameters.add(size=sizes[0], interval_us=0)
  request.response_parameters.add(size=sizes[1], interval_us=0)
  request.response_parameters.add(size=sizes[2], interval_us=0)
  return request


def FullDuplexRequest(test_pb2):
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
    protoc_command = '../../bins/%s/protobuf/protoc' % _build_mode
    protoc_plugin_filename = '../../bins/%s/grpc_python_plugin' % _build_mode
    test_proto_filename = './test.proto'
    if not os.path.isfile(protoc_command):
      # Assume that if we haven't built protoc that it's on the system.
      protoc_command = 'protoc'

    # Ensure that the output directory exists.
    self.outdir = tempfile.mkdtemp()

    # Invoke protoc with the plugin.
    cmd = [
        protoc_command,
        '--plugin=protoc-gen-python-grpc=%s' % protoc_plugin_filename,
        '-I %s' % os.path.dirname(test_proto_filename),
        '--python_out=%s' % self.outdir,
        '--python-grpc_out=%s' % self.outdir,
        os.path.basename(test_proto_filename),
    ]
    subprocess.call(' '.join(cmd), shell=True)
    sys.path.append(self.outdir)

  def tearDown(self):
    try:
      shutil.rmtree(self.outdir)
    except OSError as exc:
      if exc.errno != errno.ENOENT:
        raise

  # TODO(atash): Figure out which of theses tests is hanging flakily with small
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
        test_pb2, DOES_NOT_MATTER_DELAY) as (servicer, stub, unused_server):
      request = test_pb2.SimpleRequest(response_size=13)

  def testUnaryCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (servicer, stub, unused_server):
      request = test_pb2.SimpleRequest(response_size=13)
      response = stub.UnaryCall(request, NORMAL_TIMEOUT)
    expected_response = servicer.UnaryCall(request, None)
    self.assertEqual(expected_response, response)

  def testUnaryCallAsync(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = test_pb2.SimpleRequest(response_size=13)
    with _CreateService(test_pb2, LONG_DELAY) as (
        servicer, stub, unused_server):
      start_time = time.clock()
      response_future = stub.UnaryCall.async(request, LONG_TIMEOUT)
      # Check that we didn't block on the asynchronous call.
      self.assertGreater(LONG_DELAY, time.clock() - start_time)
      response = response_future.result()
    expected_response = servicer.UnaryCall(request, None)
    self.assertEqual(expected_response, response)

  def testUnaryCallAsyncExpired(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    # set the timeout super low...
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      request = test_pb2.SimpleRequest(response_size=13)
      with servicer.pause():
        response_future = stub.UnaryCall.async(request, SHORT_TIMEOUT)
        with self.assertRaises(exceptions.ExpirationError):
          response_future.result()

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testUnaryCallAsyncCancelled(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = test_pb2.SimpleRequest(response_size=13)
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.pause():
        response_future = stub.UnaryCall.async(request, 1)
        response_future.cancel()
        self.assertTrue(response_future.cancelled())

  def testUnaryCallAsyncFailed(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = test_pb2.SimpleRequest(response_size=13)
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.fail():
        response_future = stub.UnaryCall.async(request, NORMAL_TIMEOUT)
        self.assertIsNotNone(response_future.exception())

  def testStreamingOutputCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = StreamingOutputRequest(test_pb2)
    with _CreateService(test_pb2, NO_DELAY) as (servicer, stub, unused_server):
      responses = stub.StreamingOutputCall(request, NORMAL_TIMEOUT)
      expected_responses = servicer.StreamingOutputCall(request, None)
      for check in itertools.izip_longest(expected_responses, responses):
        expected_response, response = check
        self.assertEqual(expected_response, response)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testStreamingOutputCallExpired(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = StreamingOutputRequest(test_pb2)
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.pause():
        responses = stub.StreamingOutputCall(request, SHORT_TIMEOUT)
        with self.assertRaises(exceptions.ExpirationError):
          list(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testStreamingOutputCallCancelled(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = StreamingOutputRequest(test_pb2)
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        unused_servicer, stub, unused_server):
      responses = stub.StreamingOutputCall(request, SHORT_TIMEOUT)
      next(responses)
      responses.cancel()
      with self.assertRaises(future.CancelledError):
        next(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this times out '
                 'instead of raising the proper error.')
  def testStreamingOutputCallFailed(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = StreamingOutputRequest(test_pb2)
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.fail():
        responses = stub.StreamingOutputCall(request, 1)
        self.assertIsNotNone(responses)
        with self.assertRaises(exceptions.ServicerError):
          next(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testStreamingInputCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (servicer, stub, unused_server):
      response = stub.StreamingInputCall(StreamingInputRequest(test_pb2),
                                         NORMAL_TIMEOUT)
    expected_response = servicer.StreamingInputCall(
        StreamingInputRequest(test_pb2), None)
    self.assertEqual(expected_response, response)

  def testStreamingInputCallAsync(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, LONG_DELAY) as (
        servicer, stub, unused_server):
      start_time = time.clock()
      response_future = stub.StreamingInputCall.async(
          StreamingInputRequest(test_pb2), LONG_TIMEOUT)
      self.assertGreater(LONG_DELAY, time.clock() - start_time)
      response = response_future.result()
    expected_response = servicer.StreamingInputCall(
        StreamingInputRequest(test_pb2), None)
    self.assertEqual(expected_response, response)

  def testStreamingInputCallAsyncExpired(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    # set the timeout super low...
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.pause():
        response_future = stub.StreamingInputCall.async(
            StreamingInputRequest(test_pb2), SHORT_TIMEOUT)
        with self.assertRaises(exceptions.ExpirationError):
          response_future.result()
        self.assertIsInstance(
            response_future.exception(), exceptions.ExpirationError)

  def testStreamingInputCallAsyncCancelled(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.pause():
        response_future = stub.StreamingInputCall.async(
            StreamingInputRequest(test_pb2), NORMAL_TIMEOUT)
        response_future.cancel()
        self.assertTrue(response_future.cancelled())
      with self.assertRaises(future.CancelledError):
        response_future.result()

  def testStreamingInputCallAsyncFailed(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.fail():
        response_future = stub.StreamingInputCall.async(
            StreamingInputRequest(test_pb2), SHORT_TIMEOUT)
        self.assertIsNotNone(response_future.exception())

  def testFullDuplexCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (servicer, stub, unused_server):
      responses = stub.FullDuplexCall(FullDuplexRequest(test_pb2),
                                      NORMAL_TIMEOUT)
      expected_responses = servicer.FullDuplexCall(FullDuplexRequest(test_pb2),
                                                   None)
      for check in itertools.izip_longest(expected_responses, responses):
        expected_response, response = check
        self.assertEqual(expected_response, response)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testFullDuplexCallExpired(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = FullDuplexRequest(test_pb2)
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.pause():
        responses = stub.FullDuplexCall(request, SHORT_TIMEOUT)
        with self.assertRaises(exceptions.ExpirationError):
          list(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testFullDuplexCallCancelled(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, NO_DELAY) as (servicer, stub, unused_server):
      request = FullDuplexRequest(test_pb2)
      responses = stub.FullDuplexCall(request, NORMAL_TIMEOUT)
      next(responses)
      responses.cancel()
      with self.assertRaises(future.CancelledError):
        next(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this hangs forever '
                 'and fix.')
  def testFullDuplexCallFailed(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    request = FullDuplexRequest(test_pb2)
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      with servicer.fail():
        responses = stub.FullDuplexCall(request, NORMAL_TIMEOUT)
        self.assertIsNotNone(responses)
        with self.assertRaises(exceptions.ServicerError):
          next(responses)

  @unittest.skip('TODO(atash,nathaniel): figure out why this flakily hangs '
                 'forever and fix.')
  def testHalfDuplexCall(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    with _CreateService(test_pb2, DOES_NOT_MATTER_DELAY) as (
        servicer, stub, unused_server):
      def HalfDuplexRequest():
        request = test_pb2.StreamingOutputCallRequest()
        request.response_parameters.add(size=1, interval_us=0)
        yield request
        request = test_pb2.StreamingOutputCallRequest()
        request.response_parameters.add(size=2, interval_us=0)
        request.response_parameters.add(size=3, interval_us=0)
        yield request
      responses = stub.HalfDuplexCall(HalfDuplexRequest(), NORMAL_TIMEOUT)
      expected_responses = servicer.HalfDuplexCall(HalfDuplexRequest(), None)
      for check in itertools.izip_longest(expected_responses, responses):
        expected_response, response = check
        self.assertEqual(expected_response, response)

  def testHalfDuplexCallWedged(self):
    import test_pb2  # pylint: disable=g-import-not-at-top
    wait_flag = [False]
    @contextlib.contextmanager
    def wait():  # pylint: disable=invalid-name
      # Where's Python 3's 'nonlocal' statement when you need it?
      wait_flag[0] = True
      yield
      wait_flag[0] = False
    def HalfDuplexRequest():
      request = test_pb2.StreamingOutputCallRequest()
      request.response_parameters.add(size=1, interval_us=0)
      yield request
      while wait_flag[0]:
        time.sleep(0.1)
    with _CreateService(test_pb2, NO_DELAY) as (servicer, stub, unused_server):
      with wait():
        responses = stub.HalfDuplexCall(HalfDuplexRequest(), NORMAL_TIMEOUT)
        # half-duplex waits for the client to send all info
        with self.assertRaises(exceptions.ExpirationError):
          next(responses)


if __name__ == '__main__':
  os.chdir(os.path.dirname(sys.argv[0]))
  unittest.main(verbosity=2)
