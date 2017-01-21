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
"""Defines test client behaviors (UNARY/STREAMING) (SYNC/ASYNC)."""

import abc
import threading
import time

from concurrent import futures
from six.moves import queue

import grpc
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import services_pb2
from tests.unit import resources
from tests.unit import test_common

_TIMEOUT = 60 * 60 * 24


class GenericStub(object):

    def __init__(self, channel):
        self.UnaryCall = channel.unary_unary(
            '/grpc.testing.BenchmarkService/UnaryCall')
        self.StreamingCall = channel.stream_stream(
            '/grpc.testing.BenchmarkService/StreamingCall')


class BenchmarkClient:
    """Benchmark client interface that exposes a non-blocking send_request()."""

    __metaclass__ = abc.ABCMeta

    def __init__(self, server, config, hist):
        # Create the stub
        if config.HasField('security_params'):
            creds = grpc.ssl_channel_credentials(
                resources.test_root_certificates())
            channel = test_common.test_secure_channel(
                server, creds, config.security_params.server_host_override)
        else:
            channel = grpc.insecure_channel(server)

        # waits for the channel to be ready before we start sending messages
        grpc.channel_ready_future(channel).result()

        if config.payload_config.WhichOneof('payload') == 'simple_params':
            self._generic = False
            self._stub = services_pb2.BenchmarkServiceStub(channel)
            payload = messages_pb2.Payload(
                body='\0' * config.payload_config.simple_params.req_size)
            self._request = messages_pb2.SimpleRequest(
                payload=payload,
                response_size=config.payload_config.simple_params.resp_size)
        else:
            self._generic = True
            self._stub = GenericStub(channel)
            self._request = '\0' * config.payload_config.bytebuf_params.req_size

        self._hist = hist
        self._response_callbacks = []

    def add_response_callback(self, callback):
        """callback will be invoked as callback(client, query_time)"""
        self._response_callbacks.append(callback)

    @abc.abstractmethod
    def send_request(self):
        """Non-blocking wrapper for a client's request operation."""
        raise NotImplementedError()

    def start(self):
        pass

    def stop(self):
        pass

    def _handle_response(self, client, query_time):
        self._hist.add(query_time * 1e9)  # Report times in nanoseconds
        for callback in self._response_callbacks:
            callback(client, query_time)


class UnarySyncBenchmarkClient(BenchmarkClient):

    def __init__(self, server, config, hist):
        super(UnarySyncBenchmarkClient, self).__init__(server, config, hist)
        self._pool = futures.ThreadPoolExecutor(
            max_workers=config.outstanding_rpcs_per_channel)

    def send_request(self):
        # Send requests in seperate threads to support multiple outstanding rpcs
        # (See src/proto/grpc/testing/control.proto)
        self._pool.submit(self._dispatch_request)

    def stop(self):
        self._pool.shutdown(wait=True)
        self._stub = None

    def _dispatch_request(self):
        start_time = time.time()
        self._stub.UnaryCall(self._request, _TIMEOUT)
        end_time = time.time()
        self._handle_response(self, end_time - start_time)


class UnaryAsyncBenchmarkClient(BenchmarkClient):

    def send_request(self):
        # Use the Future callback api to support multiple outstanding rpcs
        start_time = time.time()
        response_future = self._stub.UnaryCall.future(self._request, _TIMEOUT)
        response_future.add_done_callback(
            lambda resp: self._response_received(start_time, resp))

    def _response_received(self, start_time, resp):
        resp.result()
        end_time = time.time()
        self._handle_response(self, end_time - start_time)

    def stop(self):
        self._stub = None


class _SyncStream(object):

    def __init__(self, stub, generic, request, handle_response):
        self._stub = stub
        self._generic = generic
        self._request = request
        self._handle_response = handle_response
        self._is_streaming = False
        self._request_queue = queue.Queue()
        self._send_time_queue = queue.Queue()

    def send_request(self):
        self._send_time_queue.put(time.time())
        self._request_queue.put(self._request)

    def start(self):
        self._is_streaming = True
        response_stream = self._stub.StreamingCall(self._request_generator(),
                                                   _TIMEOUT)
        for _ in response_stream:
            self._handle_response(
                self, time.time() - self._send_time_queue.get_nowait())

    def stop(self):
        self._is_streaming = False

    def _request_generator(self):
        while self._is_streaming:
            try:
                request = self._request_queue.get(block=True, timeout=1.0)
                yield request
            except queue.Empty:
                pass


class StreamingSyncBenchmarkClient(BenchmarkClient):

    def __init__(self, server, config, hist):
        super(StreamingSyncBenchmarkClient, self).__init__(server, config, hist)
        self._pool = futures.ThreadPoolExecutor(
            max_workers=config.outstanding_rpcs_per_channel)
        self._streams = [
            _SyncStream(self._stub, self._generic, self._request,
                        self._handle_response)
            for _ in xrange(config.outstanding_rpcs_per_channel)
        ]
        self._curr_stream = 0

    def send_request(self):
        # Use a round_robin scheduler to determine what stream to send on
        self._streams[self._curr_stream].send_request()
        self._curr_stream = (self._curr_stream + 1) % len(self._streams)

    def start(self):
        for stream in self._streams:
            self._pool.submit(stream.start)

    def stop(self):
        for stream in self._streams:
            stream.stop()
        self._pool.shutdown(wait=True)
        self._stub = None
