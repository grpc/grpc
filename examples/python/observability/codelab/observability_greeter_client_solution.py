# Copyright 2024 gRPC authors.
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
"""gRPC Python helloworld.Greeter client for observability codelab."""

import logging

import grpc
import grpc_observability
import helloworld_pb2
import helloworld_pb2_grpc
from opentelemetry.exporter.prometheus import PrometheusMetricReader
from opentelemetry.sdk.metrics import MeterProvider
from prometheus_client import start_http_server

_SERVER_PORT = "50051"
_PROMETHEUS_PORT = 9465


def run():
    # Start Prometheus client
    start_http_server(port=_PROMETHEUS_PORT, addr="0.0.0.0")
    # The default histogram boundaries are not granular enough for RPCs. Consider
    # override the "grpc.client.attempt.duration" view similar to csm example.
    # Link: https://github.com/grpc/grpc/blob/7407dbf21dbab125ab5eb778daab6182c009b069/examples/python/observability/csm/csm_greeter_client.py#L71C40-L71C53
    meter_provider = MeterProvider(metric_readers=[PrometheusMetricReader()])

    otel_plugin = grpc_observability.OpenTelemetryPlugin(
        meter_provider=meter_provider
    )
    otel_plugin.register_global()

    with grpc.insecure_channel(target=f"localhost:{_SERVER_PORT}") as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        # Continuously send RPCs.
        while True:
            try:
                response = stub.SayHello(
                    helloworld_pb2.HelloRequest(name="You")
                )
                print(f"Greeter client received: {response.message}")
            except grpc.RpcError as rpc_error:
                print("Call failed with code: ", rpc_error.code())

    # Deregister is not called in this example, but this is required to clean up.
    otel_plugin.deregister_global()


if __name__ == "__main__":
    logging.basicConfig()
    run()
