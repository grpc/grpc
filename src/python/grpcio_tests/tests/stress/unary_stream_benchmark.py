# Copyright 2019 The gRPC authors.
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

import contextlib
import datetime
import subprocess
import sys
import threading
import time

import grpc
import grpc.experimental

_PORT = 5741
_MESSAGE_SIZE = 4
_RESPONSE_COUNT = 32 * 1024

_SERVER_CODE = (
    """
import datetime
import threading
import grpc
from concurrent import futures
from src.python.grpcio_tests.tests.stress import unary_stream_benchmark_pb2
from src.python.grpcio_tests.tests.stress import unary_stream_benchmark_pb2_grpc

class Handler(unary_stream_benchmark_pb2_grpc.UnaryStreamBenchmarkServiceServicer):

  def Benchmark(self, request, context):
    payload = b'\\x00\\x01' * int(request.message_size / 2)
    for _ in range(request.response_count):
      yield unary_stream_benchmark_pb2.BenchmarkResponse(response=payload)


server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
server.add_insecure_port('[::]:%d')
unary_stream_benchmark_pb2_grpc.add_UnaryStreamBenchmarkServiceServicer_to_server(Handler(), server)
server.start()
server.wait_for_termination()
"""
    % _PORT
)

try:
    from src.python.grpcio_tests.tests.stress import (
        unary_stream_benchmark_pb2_grpc,
    )
    from src.python.grpcio_tests.tests.stress import unary_stream_benchmark_pb2

    _GRPC_CHANNEL_OPTIONS = [
        ("grpc.max_metadata_size", 16 * 1024 * 1024),
        ("grpc.max_receive_message_length", 64 * 1024 * 1024),
        (grpc.experimental.ChannelOptions.SingleThreadedUnaryStream, 1),
    ]

    @contextlib.contextmanager
    def _running_server():
        server_process = subprocess.Popen(
            [sys.executable, "-c", _SERVER_CODE],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            yield
        finally:
            server_process.terminate()
            server_process.wait()
            sys.stdout.write(f"stdout: {server_process.stdout.read()}")
            sys.stdout.flush()
            sys.stdout.write(f"stderr: {server_process.stderr.read()}")
            sys.stdout.flush()

    def profile(message_size, response_count):
        request = unary_stream_benchmark_pb2.BenchmarkRequest(
            message_size=message_size, response_count=response_count
        )
        with grpc.insecure_channel(
            f"[::]:{_PORT}", options=_GRPC_CHANNEL_OPTIONS
        ) as channel:
            stub = (
                unary_stream_benchmark_pb2_grpc.UnaryStreamBenchmarkServiceStub(
                    channel
                )
            )
            start = datetime.datetime.now()
            call = stub.Benchmark(request, wait_for_ready=True)
            for message in call:
                pass
            end = datetime.datetime.now()
        return end - start

    def main():
        with _running_server():
            for i in range(1000):
                latency = profile(_MESSAGE_SIZE, 1024)
                sys.stdout.write(f"{latency.total_seconds()}\n")
                sys.stdout.flush()

    if __name__ == "__main__":
        main()

except ImportError:
    # NOTE(rbellevi): The test runner should not load this module.
    pass
