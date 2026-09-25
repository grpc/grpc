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
"""Continuous load generator client for the observability dashboard.

Sends RPCs at a configurable rate to generate metrics for Prometheus/Grafana.
"""

import argparse
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

logger = logging.getLogger(__name__)

_NAMES = ["world", "gRPC", "OpenTelemetry", "Prometheus", "Grafana"]


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


def run(target: str, rps: int, prometheus_endpoint: int) -> None:
    # Start Prometheus metrics HTTP server for client-side metrics.
    start_http_server(port=prometheus_endpoint, addr="0.0.0.0")
    logger.info("Client Prometheus metrics at http://0.0.0.0:%d/metrics",
                prometheus_endpoint)

    reader = PrometheusMetricReader()
    meter_provider = MeterProvider(
        metric_readers=[reader], views=_create_views()
    )

    otel_plugin = grpc_observability.OpenTelemetryPlugin(
        meter_provider=meter_provider,
    )
    otel_plugin.register_global()

    channel = grpc.insecure_channel(target)
    stub = helloworld_pb2_grpc.GreeterStub(channel)

    interval = 1.0 / rps if rps > 0 else 1.0
    request_count = 0

    logger.info("Sending RPCs to %s at ~%d rps", target, rps)

    try:
        while True:
            name = random.choice(_NAMES)
            try:
                # Mix unary and streaming calls (80% unary, 20% streaming).
                if random.random() < 0.8:
                    response = stub.SayHello(
                        helloworld_pb2.HelloRequest(name=name)
                    )
                else:
                    responses = list(stub.SayHelloStreamReply(
                        helloworld_pb2.HelloRequest(name=name)
                    ))
            except grpc.RpcError as e:
                logger.debug("RPC failed: %s", e)

            request_count += 1
            if request_count % 100 == 0:
                logger.info("Sent %d requests", request_count)

            time.sleep(interval)
    except KeyboardInterrupt:
        logger.info("Client shutting down after %d requests", request_count)
    finally:
        channel.close()
        otel_plugin.deregister_global()


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)-8s %(message)s",
    )
    parser = argparse.ArgumentParser(
        description="gRPC load generator client with Prometheus observability."
    )
    parser.add_argument(
        "--target", type=str, default="localhost:50051",
        help="Server address.",
    )
    parser.add_argument(
        "--rps", type=int, default=10,
        help="Requests per second.",
    )
    parser.add_argument(
        "--prometheus_endpoint", type=int, default=9465,
        help="Port for the Prometheus metrics endpoint.",
    )
    args = parser.parse_args()
    run(args.target, args.rps, args.prometheus_endpoint)
