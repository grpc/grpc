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
"""gRPC Python helloworld.Greeter client with observability enabled."""

from collections import defaultdict
import logging
import time

import grpc
import grpc_observability
import helloworld_pb2
import helloworld_pb2_grpc
import open_telemetry_exporter
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics.export import PeriodicExportingMetricReader

OTEL_EXPORT_INTERVAL_S = 0.5


def run():
    all_metrics = defaultdict(list)
    otel_exporter = open_telemetry_exporter.OTelMetricExporter(all_metrics)
    reader = PeriodicExportingMetricReader(
        exporter=otel_exporter,
        export_interval_millis=OTEL_EXPORT_INTERVAL_S * 1000,
    )
    provider = MeterProvider(metric_readers=[reader])

    otel_plugin = grpc_observability.OpenTelemetryPlugin(
        meter_provider=provider
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

    # Sleep to make sure all metrics are exported.
    time.sleep(5)

    print("Metrics exported on client side:")
    for metric in all_metrics:
        print(metric)


if __name__ == "__main__":
    logging.basicConfig()
    run()
