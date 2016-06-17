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

import collections
from concurrent import futures
import contextlib
import distutils.spawn
import errno
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest

from six import moves

import grpc
from tests.unit.framework.common import test_constants

# Identifiers of entities we expect to find in the generated module.
STUB_IDENTIFIER = 'TestServiceStub'
SERVICER_IDENTIFIER = 'TestServiceServicer'
ADD_SERVICER_TO_SERVER_IDENTIFIER = 'add_TestServiceServicer_to_server'


class _ServicerMethods(object):

  def __init__(self, response_pb2, payload_pb2):
    self._condition = threading.Condition()
    self._paused = False
    self._fail = False
    self._response_pb2 = response_pb2
    self._payload_pb2 = payload_pb2

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

  def UnaryCall(self, request, unused_rpc_context):
    response = self._response_pb2.SimpleResponse()
    response.payload.payload_type = self._payload_pb2.COMPRESSABLE
    response.payload.payload_compressable = 'a' * request.response_size
    self._control()
    return response

  def StreamingOutputCall(self, request, unused_rpc_context):
    for parameter in request.response_parameters:
      response = self._response_pb2.StreamingOutputCallResponse()
      response.payload.payload_type = self._payload_pb2.COMPRESSABLE
      response.payload.payload_compressable = 'a' * parameter.size
      self._control()
      yield response

  def StreamingInputCall(self, request_iter, unused_rpc_context):
    response = self._response_pb2.StreamingInputCallResponse()
    aggregated_payload_size = 0
    for request in request_iter:
      aggregated_payload_size += len(request.payload.payload_compressable)
    response.aggregated_payload_size = aggregated_payload_size
    self._control()
    return response

  def FullDuplexCall(self, request_iter, unused_rpc_context):
    for request in request_iter:
      for parameter in request.response_parameters:
        response = self._response_pb2.StreamingOutputCallResponse()
        response.payload.payload_type = self._payload_pb2.COMPRESSABLE
        response.payload.payload_compressable = 'a' * parameter.size
        self._control()
        yield response

  def HalfDuplexCall(self, request_iter, unused_rpc_context):
    responses = []
    for request in request_iter:
      for parameter in request.response_parameters:
        response = self._response_pb2.StreamingOutputCallResponse()
        response.payload.payload_type = self._payload_pb2.COMPRESSABLE
        response.payload.payload_compressable = 'a' * parameter.size
        self._control()
        responses.append(response)
    for response in responses:
      yield response


class _Service(
    collections.namedtuple(
      '_Service', ('servicer_methods', 'server', 'stub',))):
  """A live and running service.

  Attributes:
    servicer_methods: The _ServicerMethods servicing RPCs.
    server: The grpc.Server servicing RPCs.
    stub: A stub on which to invoke RPCs.
  """
      

def _CreateService(service_pb2, response_pb2, payload_pb2):
  """Provides a servicer backend and a stub.

  Args:
    service_pb2: The service_pb2 module generated by this test.
    response_pb2: The response_pb2 module generated by this test.
    payload_pb2: The payload_pb2 module generated by this test.

  Returns:
    A _Service with which to test RPCs.
  """
  servicer_methods = _ServicerMethods(response_pb2, payload_pb2)

  class Servicer(getattr(service_pb2, SERVICER_IDENTIFIER)):

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

  server = grpc.server(
      (), futures.ThreadPoolExecutor(max_workers=test_constants.POOL_SIZE))
  getattr(service_pb2, ADD_SERVICER_TO_SERVER_IDENTIFIER)(Servicer(), server)
  port = server.add_insecure_port('[::]:0')
  server.start()
  channel = grpc.insecure_channel('localhost:{}'.format(port))
  stub = getattr(service_pb2, STUB_IDENTIFIER)(channel)
  return _Service(servicer_methods, server, stub)


def _CreateIncompleteService(service_pb2):
  """Provides a servicer backend that fails to implement methods and its stub.

  Args:
    service_pb2: The service_pb2 module generated by this test.

  Returns:
    A _Service with which to test RPCs. The returned _Service's
      servicer_methods implements none of the methods required of it.
  """

  class Servicer(getattr(service_pb2, SERVICER_IDENTIFIER)):
    pass

  server = grpc.server(
      (), futures.ThreadPoolExecutor(max_workers=test_constants.POOL_SIZE))
  getattr(service_pb2, ADD_SERVICER_TO_SERVER_IDENTIFIER)(Servicer(), server)
  port = server.add_insecure_port('[::]:0')
  server.start()
  channel = grpc.insecure_channel('localhost:{}'.format(port))
  stub = getattr(service_pb2, STUB_IDENTIFIER)(channel)
  return _Service(None, server, stub)


def _streaming_input_request_iterator(request_pb2, payload_pb2):
  for _ in range(3):
    request = request_pb2.StreamingInputCallRequest()
    request.payload.payload_type = payload_pb2.COMPRESSABLE
    request.payload.payload_compressable = 'a'
    yield request


def _streaming_output_request(request_pb2):
  request = request_pb2.StreamingOutputCallRequest()
  sizes = [1, 2, 3]
  request.response_parameters.add(size=sizes[0], interval_us=0)
  request.response_parameters.add(size=sizes[1], interval_us=0)
  request.response_parameters.add(size=sizes[2], interval_us=0)
  return request


def _full_duplex_request_iterator(request_pb2):
  request = request_pb2.StreamingOutputCallRequest()
  request.response_parameters.add(size=1, interval_us=0)
  yield request
  request = request_pb2.StreamingOutputCallRequest()
  request.response_parameters.add(size=2, interval_us=0)
  request.response_parameters.add(size=3, interval_us=0)
  yield request


class PythonPluginTest(unittest.TestCase):
  """Test case for the gRPC Python protoc-plugin.

  While reading these tests, remember that the futures API
  (`stub.method.future()`) only gives futures for the *response-unary*
  methods and does not exist for response-streaming methods.
  """

  def setUp(self):
    # Assume that the appropriate protoc and grpc_python_plugins are on the
    # path.
    protoc_command = 'protoc'
    protoc_plugin_filename = distutils.spawn.find_executable(
        'grpc_python_plugin')
    if not os.path.isfile(protoc_command):
      # Assume that if we haven't built protoc that it's on the system.
      protoc_command = 'protoc'

    # Ensure that the output directory exists.
    self.outdir = tempfile.mkdtemp()

    # Find all proto files
    paths = []
    root_dir = os.path.dirname(os.path.realpath(__file__))
    proto_dir = os.path.join(root_dir, 'protos')
    for walk_root, _, filenames in os.walk(proto_dir):
      for filename in filenames:
        if filename.endswith('.proto'):
          path = os.path.join(walk_root, filename)
          paths.append(path)

    # Invoke protoc with the plugin.
    cmd = [
        protoc_command,
        '--plugin=protoc-gen-python-grpc=%s' % protoc_plugin_filename,
        '-I %s' % root_dir,
        '--python_out=%s' % self.outdir,
        '--python-grpc_out=%s' % self.outdir
    ] + paths
    subprocess.check_call(' '.join(cmd), shell=True, env=os.environ,
                          cwd=os.path.dirname(os.path.realpath(__file__)))

    # Generated proto directories dont include __init__.py, but
    # these are needed for python package resolution
    for walk_root, _, _ in os.walk(os.path.join(self.outdir, 'protos')):
      path = os.path.join(walk_root, '__init__.py')
      open(path, 'a').close()

    sys.path.insert(0, self.outdir)

    import protos.payload.test_payload_pb2 as payload_pb2
    import protos.requests.r.test_requests_pb2 as request_pb2
    import protos.responses.test_responses_pb2 as response_pb2
    import protos.service.test_service_pb2 as service_pb2
    self._payload_pb2 = payload_pb2
    self._request_pb2 = request_pb2
    self._response_pb2 = response_pb2
    self._service_pb2 = service_pb2

  def tearDown(self):
    try:
      shutil.rmtree(self.outdir)
    except OSError as exc:
      if exc.errno != errno.ENOENT:
        raise
    sys.path.remove(self.outdir)

  def testImportAttributes(self):
    # check that we can access the generated module and its members.
    self.assertIsNotNone(
        getattr(self._service_pb2, STUB_IDENTIFIER, None))
    self.assertIsNotNone(
        getattr(self._service_pb2, SERVICER_IDENTIFIER, None))
    self.assertIsNotNone(
        getattr(self._service_pb2, ADD_SERVICER_TO_SERVER_IDENTIFIER, None))

  def testUpDown(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    self.assertIsNotNone(service.servicer_methods)
    self.assertIsNotNone(service.server)
    self.assertIsNotNone(service.stub)

  def testIncompleteServicer(self):
    service = _CreateIncompleteService(self._service_pb2)
    request = self._request_pb2.SimpleRequest(response_size=13)
    with self.assertRaises(grpc.RpcError) as exception_context:
      service.stub.UnaryCall(request)
    self.assertIs(
        exception_context.exception.code(), grpc.StatusCode.UNIMPLEMENTED)

  def testUnaryCall(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = self._request_pb2.SimpleRequest(response_size=13)
    response = service.stub.UnaryCall(request)
    expected_response = service.servicer_methods.UnaryCall(
        request, 'not a real context!')
    self.assertEqual(expected_response, response)

  def testUnaryCallFuture(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = self._request_pb2.SimpleRequest(response_size=13)
    # Check that the call does not block waiting for the server to respond.
    with service.servicer_methods.pause():
      response_future = service.stub.UnaryCall.future(request)
    response = response_future.result()
    expected_response = service.servicer_methods.UnaryCall(
        request, 'not a real RpcContext!')
    self.assertEqual(expected_response, response)

  def testUnaryCallFutureExpired(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = self._request_pb2.SimpleRequest(response_size=13)
    with service.servicer_methods.pause():
      response_future = service.stub.UnaryCall.future(
          request, timeout=test_constants.SHORT_TIMEOUT)
      with self.assertRaises(grpc.RpcError) as exception_context:
        response_future.result()
    self.assertIs(
        exception_context.exception.code(), grpc.StatusCode.DEADLINE_EXCEEDED)
    self.assertIs(response_future.code(), grpc.StatusCode.DEADLINE_EXCEEDED)

  def testUnaryCallFutureCancelled(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = self._request_pb2.SimpleRequest(response_size=13)
    with service.servicer_methods.pause():
      response_future = service.stub.UnaryCall.future(request)
      response_future.cancel()
    self.assertTrue(response_future.cancelled())
    self.assertIs(response_future.code(), grpc.StatusCode.CANCELLED)

  def testUnaryCallFutureFailed(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = self._request_pb2.SimpleRequest(response_size=13)
    with service.servicer_methods.fail():
      response_future = service.stub.UnaryCall.future(request)
      self.assertIsNotNone(response_future.exception())
    self.assertIs(response_future.code(), grpc.StatusCode.UNKNOWN)

  def testStreamingOutputCall(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = _streaming_output_request(self._request_pb2)
    responses = service.stub.StreamingOutputCall(request)
    expected_responses = service.servicer_methods.StreamingOutputCall(
        request, 'not a real RpcContext!')
    for expected_response, response in moves.zip_longest(
        expected_responses, responses):
      self.assertEqual(expected_response, response)

  def testStreamingOutputCallExpired(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = _streaming_output_request(self._request_pb2)
    with service.servicer_methods.pause():
      responses = service.stub.StreamingOutputCall(
          request, timeout=test_constants.SHORT_TIMEOUT)
      with self.assertRaises(grpc.RpcError) as exception_context:
        list(responses)
    self.assertIs(
        exception_context.exception.code(), grpc.StatusCode.DEADLINE_EXCEEDED)

  def testStreamingOutputCallCancelled(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = _streaming_output_request(self._request_pb2)
    responses = service.stub.StreamingOutputCall(request)
    next(responses)
    responses.cancel()
    with self.assertRaises(grpc.RpcError) as exception_context:
      next(responses)
    self.assertIs(responses.code(), grpc.StatusCode.CANCELLED)

  def testStreamingOutputCallFailed(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request = _streaming_output_request(self._request_pb2)
    with service.servicer_methods.fail():
      responses = service.stub.StreamingOutputCall(request)
      self.assertIsNotNone(responses)
      with self.assertRaises(grpc.RpcError) as exception_context:
        next(responses)
    self.assertIs(exception_context.exception.code(), grpc.StatusCode.UNKNOWN)

  def testStreamingInputCall(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    response = service.stub.StreamingInputCall(
        _streaming_input_request_iterator(
            self._request_pb2, self._payload_pb2))
    expected_response = service.servicer_methods.StreamingInputCall(
        _streaming_input_request_iterator(self._request_pb2, self._payload_pb2),
        'not a real RpcContext!')
    self.assertEqual(expected_response, response)

  def testStreamingInputCallFuture(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    with service.servicer_methods.pause():
      response_future = service.stub.StreamingInputCall.future(
          _streaming_input_request_iterator(
              self._request_pb2, self._payload_pb2))
    response = response_future.result()
    expected_response = service.servicer_methods.StreamingInputCall(
        _streaming_input_request_iterator(self._request_pb2, self._payload_pb2),
        'not a real RpcContext!')
    self.assertEqual(expected_response, response)

  def testStreamingInputCallFutureExpired(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    with service.servicer_methods.pause():
      response_future = service.stub.StreamingInputCall.future(
          _streaming_input_request_iterator(
              self._request_pb2, self._payload_pb2),
          timeout=test_constants.SHORT_TIMEOUT)
      with self.assertRaises(grpc.RpcError) as exception_context:
        response_future.result()
    self.assertIsInstance(response_future.exception(), grpc.RpcError)
    self.assertIs(
        response_future.exception().code(), grpc.StatusCode.DEADLINE_EXCEEDED)
    self.assertIs(
        exception_context.exception.code(), grpc.StatusCode.DEADLINE_EXCEEDED)

  def testStreamingInputCallFutureCancelled(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    with service.servicer_methods.pause():
      response_future = service.stub.StreamingInputCall.future(
          _streaming_input_request_iterator(
              self._request_pb2, self._payload_pb2))
      response_future.cancel()
    self.assertTrue(response_future.cancelled())
    with self.assertRaises(grpc.FutureCancelledError):
      response_future.result()

  def testStreamingInputCallFutureFailed(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    with service.servicer_methods.fail():
      response_future = service.stub.StreamingInputCall.future(
          _streaming_input_request_iterator(
              self._request_pb2, self._payload_pb2))
      self.assertIsNotNone(response_future.exception())
      self.assertIs(response_future.code(), grpc.StatusCode.UNKNOWN)

  def testFullDuplexCall(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    responses = service.stub.FullDuplexCall(
        _full_duplex_request_iterator(self._request_pb2))
    expected_responses = service.servicer_methods.FullDuplexCall(
        _full_duplex_request_iterator(self._request_pb2),
        'not a real RpcContext!')
    for expected_response, response in moves.zip_longest(
        expected_responses, responses):
      self.assertEqual(expected_response, response)

  def testFullDuplexCallExpired(self):
    request_iterator = _full_duplex_request_iterator(self._request_pb2)
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    with service.servicer_methods.pause():
      responses = service.stub.FullDuplexCall(
          request_iterator, timeout=test_constants.SHORT_TIMEOUT)
      with self.assertRaises(grpc.RpcError) as exception_context:
        list(responses)
    self.assertIs(
        exception_context.exception.code(), grpc.StatusCode.DEADLINE_EXCEEDED)

  def testFullDuplexCallCancelled(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    request_iterator = _full_duplex_request_iterator(self._request_pb2)
    responses = service.stub.FullDuplexCall(request_iterator)
    next(responses)
    responses.cancel()
    with self.assertRaises(grpc.RpcError) as exception_context:
      next(responses)
    self.assertIs(
        exception_context.exception.code(), grpc.StatusCode.CANCELLED)

  def testFullDuplexCallFailed(self):
    request_iterator = _full_duplex_request_iterator(self._request_pb2)
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    with service.servicer_methods.fail():
      responses = service.stub.FullDuplexCall(request_iterator)
      with self.assertRaises(grpc.RpcError) as exception_context:
        next(responses)
    self.assertIs(exception_context.exception.code(), grpc.StatusCode.UNKNOWN)

  def testHalfDuplexCall(self):
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    def half_duplex_request_iterator():
      request = self._request_pb2.StreamingOutputCallRequest()
      request.response_parameters.add(size=1, interval_us=0)
      yield request
      request = self._request_pb2.StreamingOutputCallRequest()
      request.response_parameters.add(size=2, interval_us=0)
      request.response_parameters.add(size=3, interval_us=0)
      yield request
    responses = service.stub.HalfDuplexCall(half_duplex_request_iterator())
    expected_responses = service.servicer_methods.HalfDuplexCall(
        half_duplex_request_iterator(), 'not a real RpcContext!')
    for expected_response, response in moves.zip_longest(
        expected_responses, responses):
      self.assertEqual(expected_response, response)

  def testHalfDuplexCallWedged(self):
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
      request = self._request_pb2.StreamingOutputCallRequest()
      request.response_parameters.add(size=1, interval_us=0)
      yield request
      with condition:
        while wait_cell[0]:
          condition.wait()
    service = _CreateService(
        self._service_pb2, self._response_pb2, self._payload_pb2)
    with wait():
      responses = service.stub.HalfDuplexCall(
          half_duplex_request_iterator(), timeout=test_constants.SHORT_TIMEOUT)
      # half-duplex waits for the client to send all info
      with self.assertRaises(grpc.RpcError) as exception_context:
        next(responses)
    self.assertIs(
        exception_context.exception.code(), grpc.StatusCode.DEADLINE_EXCEEDED)


if __name__ == '__main__':
  unittest.main(verbosity=2)
