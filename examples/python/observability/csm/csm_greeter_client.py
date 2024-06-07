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

import grpc
from grpc_csm_observability import CsmOpenTelemetryPlugin
from opentelemetry.exporter.prometheus import PrometheusMetricReader
from opentelemetry.sdk.metrics import MeterProvider
from prometheus_client import start_http_server

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt="%(asctime)s: %(levelname)-8s %(message)s")
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)


def _run(target: int, secure_mode: bool, prometheus_endpoint: int):
    csm_plugin = _prepare_csm_observability_plugin(prometheus_endpoint)
    csm_plugin.register_global()
    if secure_mode:
        fallback_creds = grpc.experimental.insecure_channel_credentials()
        channel_creds = grpc.xds_channel_credentials(fallback_creds)
        channel = grpc.secure_channel(target, channel_creds)
    else:
        channel = grpc.insecure_channel(target)
    with channel:
        stub = test_pb2_grpc.TestServiceStub(channel)
        # Continuously send RPCs every second.
        while True:
            request = messages_pb2.SimpleRequest()
            logger.info("Sending request to server")
            stub.UnaryCall(request)
            time.sleep(1)
    csm_plugin.deregister_global()


def _prepare_csm_observability_plugin(
    prometheus_endpoint: int,
) -> CsmOpenTelemetryPlugin:
    # Start Prometheus client
    start_http_server(port=prometheus_endpoint, addr="0.0.0.0")
    reader = PrometheusMetricReader()
    meter_provider = MeterProvider(metric_readers=[reader])
    csm_plugin = CsmOpenTelemetryPlugin(
        meter_provider=meter_provider,
    )
    return csm_plugin


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
        default="xds:///helloworld:50051",
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
