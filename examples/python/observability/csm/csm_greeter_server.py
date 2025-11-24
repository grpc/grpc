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

import argparse
from concurrent import futures
import logging
from typing import Sequence

import grpc
from grpc_csm_observability import CsmOpenTelemetryPlugin
from opentelemetry.exporter.prometheus import PrometheusMetricReader
from opentelemetry.sdk.metrics import Histogram
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics import view
from prometheus_client import start_http_server

from examples.python.observability.csm import helloworld_pb2
from examples.python.observability.csm import helloworld_pb2_grpc

_LISTEN_HOST = "0.0.0.0"
_THREAD_POOL_SIZE = 256

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt="%(asctime)s: %(levelname)-8s %(message)s")
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)


class Greeter(helloworld_pb2_grpc.GreeterServicer):
    def SayHello(self, request, context):
        message = request.name
        return helloworld_pb2.HelloReply(message=f"Hello {message}")


def _run(
    port: int,
    secure_mode: bool,
    server_id: str,
    prometheus_endpoint: int,
) -> None:
    csm_plugin = _prepare_csm_observability_plugin(prometheus_endpoint)
    csm_plugin.register_global()
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=_THREAD_POOL_SIZE),
        xds=secure_mode,
    )
    _configure_test_server(server, port, secure_mode, server_id)
    server.start()
    logger.info("Test server listening on port %d", port)
    server.wait_for_termination()
    csm_plugin.deregister_global()


def _prepare_csm_observability_plugin(
    prometheus_endpoint: int,
) -> CsmOpenTelemetryPlugin:
    # Start Prometheus client
    start_http_server(port=prometheus_endpoint, addr="0.0.0.0")
    reader = PrometheusMetricReader()
    meter_provider = MeterProvider(
        metric_readers=[reader], views=_create_views()
    )
    csm_plugin = CsmOpenTelemetryPlugin(
        meter_provider=meter_provider,
    )
    return csm_plugin


def _create_views() -> Sequence[view.View]:
    """Create a list of views with config for specific metrics."""
    latency_boundaries = [
        0,
        0.00001,
        0.00005,
        0.0001,
        0.0003,
        0.0006,
        0.0008,
        0.001,
        0.002,
        0.003,
        0.004,
        0.005,
        0.006,
        0.008,
        0.01,
        0.013,
        0.016,
        0.02,
        0.025,
        0.03,
        0.04,
        0.05,
        0.065,
        0.08,
        0.1,
        0.13,
        0.16,
        0.2,
        0.25,
        0.3,
        0.4,
        0.5,
        0.65,
        0.8,
        1,
        2,
        5,
        10,
        20,
        50,
        100,
    ]
    size_boundaries = [
        0,
        1024,
        2048,
        4096,
        16384,
        65536,
        262144,
        1048576,
        4194304,
        16777216,
        67108864,
        268435456,
        1073741824,
        4294967296,
    ]
    return [
        view.View(
            instrument_type=Histogram,
            instrument_unit="s",
            aggregation=view.ExplicitBucketHistogramAggregation(
                # Boundaries as defined in gRFC. See:
                # https://github.com/grpc/proposal/blob/master/A66-otel-stats.md
                boundaries=latency_boundaries
            ),
        ),
        view.View(
            instrument_type=Histogram,
            instrument_unit="By",
            aggregation=view.ExplicitBucketHistogramAggregation(
                # Boundaries as defined in gRFC. See:
                # https://github.com/grpc/proposal/blob/master/A66-otel-stats.md
                boundaries=size_boundaries
            ),
        ),
    ]


def _configure_test_server(
    server: grpc.Server, port: int, secure_mode: bool, server_id: str
) -> None:
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    listen_address = f"{_LISTEN_HOST}:{port}"
    if not secure_mode:
        server.add_insecure_port(listen_address)
    else:
        logger.info("Running with xDS Server credentials")
        server_fallback_creds = grpc.insecure_server_credentials()
        server_creds = grpc.xds_server_credentials(server_fallback_creds)
        server.add_secure_port(listen_address, server_creds)


def bool_arg(arg: str) -> bool:
    if arg.lower() in ("true", "yes", "y"):
        return True
    elif arg.lower() in ("false", "no", "n"):
        return False
    else:
        raise argparse.ArgumentTypeError(f"Could not parse '{arg}' as a bool.")


if __name__ == "__main__":
    logging.basicConfig()
    logger.setLevel(logging.INFO)
    parser = argparse.ArgumentParser(
        description="Run Python CSM Observability Test server."
    )
    parser.add_argument(
        "--port", type=int, default=50051, help="Port for test server."
    )
    parser.add_argument(
        "--secure_mode",
        type=bool_arg,
        default="False",
        help="If specified, uses xDS to retrieve server credentials.",
    )
    parser.add_argument(
        "--server_id",
        type=str,
        default="python_server",
        help="The server ID to return in responses.",
    )
    parser.add_argument(
        "--prometheus_endpoint",
        type=int,
        default=9464,
        help="Port for servers besides test server.",
    )
    args = parser.parse_args()
    _run(
        args.port,
        args.secure_mode,
        args.server_id,
        args.prometheus_endpoint,
    )
