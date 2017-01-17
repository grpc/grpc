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

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import services_pb2


class BenchmarkServer(services_pb2.BenchmarkServiceServicer):
    """Synchronous Server implementation for the Benchmark service."""

    def UnaryCall(self, request, context):
        payload = messages_pb2.Payload(body='\0' * request.response_size)
        return messages_pb2.SimpleResponse(payload=payload)

    def StreamingCall(self, request_iterator, context):
        for request in request_iterator:
            payload = messages_pb2.Payload(body='\0' * request.response_size)
            yield messages_pb2.SimpleResponse(payload=payload)


class GenericBenchmarkServer(services_pb2.BenchmarkServiceServicer):
    """Generic Server implementation for the Benchmark service."""

    def __init__(self, resp_size):
        self._response = '\0' * resp_size

    def UnaryCall(self, request, context):
        return self._response

    def StreamingCall(self, request_iterator, context):
        for request in request_iterator:
            yield self._response
