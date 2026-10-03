# Copyright 2024 The gRPC authors.
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
"""gRPC server with Prometheus metrics for the observability dashboard.

This server exports OpenTelemetry metrics via a Prometheus endpoint,
which is scraped by Prometheus and visualized in Grafana.
"""

import argparse
from concurrent import futures
import logging
import random
import time
from typing import Sequence

import grpc
import grpc_observability
from opentelemetry.exporter.prometheus import PrometheusMetricReader
from opentelemetry.sdk.metrics import Histogram
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics import view
from prometheus_client import start_http_server

import helloworld_pb2
import helloworld_pb2_grpc

_LISTEN_HOST = "0.0.0.0"
_THREAD_POOL_SIZE = 10

logger = logging.getLogger(__name__)


class Greeter(helloworld_pb2_grpc.GreeterServicer):
    """Greeter servicer that introduces random latency and occasional errors
    to generate varied metrics for the dashboard."""

    def SayHello(self, request, context):
        # Add random latency (0-100ms) for varied histogram data.
        time.sleep(random.uniform(0, 0.1))

        # Simulate occasional errors (~5% of requests).
        if random.random() < 0.05:
            context.abort(grpc.StatusCode.UNAVAILABLE, "Simulated error")

        return helloworld_pb2.HelloReply(
            message=f"Hello {request.name}"
        )

    def SayHelloStreamReply(self, request, context):
        for i in range(3):
            time.sleep(random.uniform(0, 0.03))
            yield helloworld_pb2.HelloReply(
                message=f"Hello {request.name} ({i})"
            )


def _create_views() -> Sequence[view.View]:
    """Create histogram views with bucket boundaries from gRFC A66."""
    latency_boundaries = [
        0, 0.00001, 0.00005, 0.0001, 0.0003, 0.0006, 0.0008, 0.001,
        0.002, 0.003, 0.004, 0.005, 0.006, 0.008, 0.01, 0.013, 0.016,
        0.02, 0.025, 0.03, 0.04, 0.05, 0.065, 0.08, 0.1, 0.13, 0.16,
        0.2, 0.25, 0.3, 0.4, 0.5, 0.65, 0.8, 1, 2, 5, 10, 20, 50, 100,
    ]
    size_boundaries = [
        0, 1024, 2048, 4096, 16384, 65536, 262144, 1048576,
        4194304, 16777216, 67108864, 268435456, 1073741824, 4294967296,
    ]
    return [
        view.View(
            instrument_type=Histogram,
            instrument_unit="s",
            aggregation=view.ExplicitBucketHistogramAggregation(
                boundaries=latency_boundaries
            ),
        ),
        view.View(
            instrument_type=Histogram,
            instrument_unit="By",
            aggregation=view.ExplicitBucketHistogramAggregation(
                boundaries=size_boundaries
            ),
        ),
    ]


def serve(port: int, prometheus_endpoint: int) -> None:
    # Start Prometheus metrics HTTP server.
    start_http_server(port=prometheus_endpoint, addr=_LISTEN_HOST)
    logger.info("Prometheus metrics at http://%s:%d/metrics",
                _LISTEN_HOST, prometheus_endpoint)

    reader = PrometheusMetricReader()
    meter_provider = MeterProvider(
        metric_readers=[reader], views=_create_views()
    )

    otel_plugin = grpc_observability.OpenTelemetryPlugin(
        meter_provider=meter_provider,
    )
    otel_plugin.register_global()

    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=_THREAD_POOL_SIZE),
    )
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    server.add_insecure_port(f"{_LISTEN_HOST}:{port}")
    server.start()
    logger.info("gRPC server listening on port %d", port)

    try:
        server.wait_for_termination()
    finally:
        otel_plugin.deregister_global()


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)-8s %(message)s",
    )
    parser = argparse.ArgumentParser(
        description="gRPC Greeter server with Prometheus observability."
    )
    parser.add_argument(
        "--port", type=int, default=50051,
        help="Port for the gRPC server.",
    )
    parser.add_argument(
        "--prometheus_endpoint", type=int, default=9464,
        help="Port for the Prometheus metrics endpoint.",
    )
    args = parser.parse_args()
    serve(args.port, args.prometheus_endpoint)
