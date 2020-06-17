# Copyright 2020 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""The Python AsyncIO Benchmark Servicers."""

import asyncio
import logging
import unittest

from grpc.experimental import aio

from src.proto.grpc.testing import benchmark_service_pb2_grpc, messages_pb2


class BenchmarkServicer(benchmark_service_pb2_grpc.BenchmarkServiceServicer):

    async def UnaryCall(self, request, unused_context):
        payload = messages_pb2.Payload(body=b'\0' * request.response_size)
        return messages_pb2.SimpleResponse(payload=payload)

    async def StreamingFromServer(self, request, unused_context):
        payload = messages_pb2.Payload(body=b'\0' * request.response_size)
        # Sends response at full capacity!
        while True:
            yield messages_pb2.SimpleResponse(payload=payload)

    async def StreamingCall(self, request_iterator, unused_context):
        async for request in request_iterator:
            payload = messages_pb2.Payload(body=b'\0' * request.response_size)
            yield messages_pb2.SimpleResponse(payload=payload)


class GenericBenchmarkServicer(
        benchmark_service_pb2_grpc.BenchmarkServiceServicer):
    """Generic (no-codec) Server implementation for the Benchmark service."""

    def __init__(self, resp_size):
        self._response = '\0' * resp_size

    async def UnaryCall(self, unused_request, unused_context):
        return self._response

    async def StreamingCall(self, request_iterator, unused_context):
        async for _ in request_iterator:
            yield self._response
