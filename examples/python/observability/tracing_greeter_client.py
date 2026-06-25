# Copyright 2026 gRPC authors.
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
"""gRPC Python helloworld.Greeter client with OpenTelemetry tracing enabled."""

import logging
import time

import grpc
import grpc_observability
import helloworld_pb2
import helloworld_pb2_grpc
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import ConsoleSpanExporter
from opentelemetry.sdk.trace.export import SimpleSpanProcessor
from opentelemetry.trace.propagation.tracecontext import (
    TraceContextTextMapPropagator,
)


def run():
    tracer_provider = TracerProvider()
    tracer_provider.add_span_processor(
        SimpleSpanProcessor(ConsoleSpanExporter())
    )

    otel_plugin = grpc_observability.OpenTelemetryPlugin(
        tracer_provider=tracer_provider,
        text_map_propagator=TraceContextTextMapPropagator(),
    )
    otel_plugin.register_global()

    with grpc.insecure_channel(target="localhost:50051") as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        try:
            response = stub.SayHello(helloworld_pb2.HelloRequest(name="You"))
            print(f"Greeter client received: {response.message}")
        except grpc.RpcError as rpc_error:
            print("Call failed with code: ", rpc_error.code())
    otel_plugin.deregister_global()

    # Sleep to make sure all spans are exported.
    time.sleep(5)


if __name__ == "__main__":
    logging.basicConfig()
    run()
