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
"""The Python implementation of the GRPC helloworld.Greeter server with observability enabled."""

from collections import defaultdict
from concurrent import futures
import logging
import time

import grpc
import grpc_observability
import helloworld_pb2
import helloworld_pb2_grpc
import open_telemetry_exporter
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics.export import PeriodicExportingMetricReader

_OTEL_EXPORT_INTERVAL_S = 0.5
_SERVER_PORT = "50051"


class Greeter(helloworld_pb2_grpc.GreeterServicer):
    def SayHello(self, request, context):
        message = request.name
        return helloworld_pb2.HelloReply(message=f"Hello {message}")


def serve():
    all_metrics = defaultdict(list)
    otel_exporter = open_telemetry_exporter.OTelMetricExporter(
        all_metrics, print_live=False
    )
    reader = PeriodicExportingMetricReader(
        exporter=otel_exporter,
        export_interval_millis=_OTEL_EXPORT_INTERVAL_S * 1000,
    )
    provider = MeterProvider(metric_readers=[reader])

    otel_plugin = grpc_observability.OpenTelemetryPlugin(
        meter_provider=provider
    )
    otel_plugin.register_global()

    server = grpc.server(
        thread_pool=futures.ThreadPoolExecutor(max_workers=10),
    )
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    server.add_insecure_port("[::]:" + _SERVER_PORT)
    server.start()
    print("Server started, listening on " + _SERVER_PORT)

    # Sleep to make sure client made RPC call and all metrics are exported.
    time.sleep(10)
    print("Metrics exported on Server side:")
    for metric in all_metrics:
        print(metric)

    server.stop(0)
    otel_plugin.deregister_global()


if __name__ == "__main__":
    logging.basicConfig()
    serve()
