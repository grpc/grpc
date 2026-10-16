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
import logging
import time
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

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt="%(asctime)s: %(levelname)-8s %(message)s")
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)


def _run(target: str, secure_mode: bool, prometheus_endpoint: int):
    csm_plugin = _prepare_csm_observability_plugin(prometheus_endpoint)
    csm_plugin.register_global()
    if secure_mode:
        fallback_creds = grpc.experimental.insecure_channel_credentials()
        channel_creds = grpc.xds_channel_credentials(fallback_creds)
        channel = grpc.secure_channel(target, channel_creds)
    else:
        channel = grpc.insecure_channel(target)
    with channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        # Continuously send RPCs every second.
        while True:
            request = helloworld_pb2.HelloRequest(name="You")
            logger.info("Sending request to server")
            try:
                response = stub.SayHello(request)
                print(f"Greeter client received: {response.message}")
                time.sleep(1)
            except Exception:  # pylint: disable=broad-except
                logger.info(
                    "Request failed, this is normal during initial setup."
                )
    # Deregister is not called in this example, but this is required to clean up.
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
        description="Run Python CSM Observability Test client."
    )
    parser.add_argument(
        "--target",
        default="localhost:50051",
        help="The address of the server.",
    )
    parser.add_argument(
        "--secure_mode",
        default="False",
        type=bool_arg,
        help="If specified, uses xDS credentials to connect to the server.",
    )
    parser.add_argument(
        "--prometheus_endpoint",
        type=int,
        default=9464,
        help="Port for servers besides test server.",
    )
    args = parser.parse_args()
    _run(args.target, args.secure_mode, args.prometheus_endpoint)
