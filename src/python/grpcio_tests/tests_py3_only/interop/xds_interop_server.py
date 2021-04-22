# Copyright 2021 The gRPC authors.
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
import signal
import threading
import time
import socket
import sys

from typing import DefaultDict, Dict, List, Mapping, Set, Sequence, Tuple
import collections

from concurrent import futures

import grpc
from grpc_channelz.v1 import channelz
from grpc_channelz.v1 import channelz_pb2
from grpc_health.v1 import health_pb2, health_pb2_grpc
from grpc_health.v1 import health as grpc_health
from grpc_reflection.v1alpha import reflection

from src.proto.grpc.testing import test_pb2
from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import empty_pb2

# NOTE: This interop server is not fully compatible with all xDS interop tests.
#  It currently only implements enough functionality to pass the xDS security
#  tests.

_LISTEN_HOST = "[::]"

_THREAD_POOL_SIZE = 256

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt='%(asctime)s: %(levelname)-8s %(message)s')
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)


class TestService(test_pb2_grpc.TestServiceServicer):

    def __init__(self, server_id, hostname):
        self._server_id = server_id
        self._hostname = hostname

    def EmptyCall(self, _: empty_pb2.Empty,
                  context: grpc.ServicerContext) -> empty_pb2.Empty:
        return empty_pb2.Empty()

    def UnaryCall(self, request: messages_pb2.SimpleRequest,
                  context: grpc.ServicerContext) -> messages_pb2.SimpleResponse:
        response = messages_pb2.SimpleResponse()
        response.server_id = self._server_id
        response.hostname = self._hostname
        return response


def _configure_maintenance_server(server: grpc.Server,
                                  maintenance_port: int) -> None:
    channelz.add_channelz_servicer(server)
    listen_address = f"{_LISTEN_HOST}:{maintenance_port}"
    server.add_insecure_port(listen_address)
    health_servicer = grpc_health.HealthServicer(
        experimental_non_blocking=True,
        experimental_thread_pool=futures.ThreadPoolExecutor(
            max_workers=_THREAD_POOL_SIZE))

    health_pb2_grpc.add_HealthServicer_to_server(health_servicer, server)
    SERVICE_NAMES = (
        test_pb2.DESCRIPTOR.services_by_name["TestService"].full_name,
        health_pb2.DESCRIPTOR.services_by_name["Health"].full_name,
        channelz_pb2.DESCRIPTOR.services_by_name["Channelz"].full_name,
        reflection.SERVICE_NAME,
    )
    for service in SERVICE_NAMES:
        health_servicer.set(service, health_pb2.HealthCheckResponse.SERVING)
    reflection.enable_server_reflection(SERVICE_NAMES, server)


def _configure_test_server(server: grpc.Server, port: int, secure_mode: bool,
                           server_id: str) -> None:
    test_pb2_grpc.add_TestServiceServicer_to_server(
        TestService(server_id, socket.gethostname()), server)
    listen_address = f"{_LISTEN_HOST}:{port}"
    if not secure_mode:
        server.add_insecure_port(listen_address)
    else:
        logger.info("Running with xDS Server credentials")
        server_fallback_creds = grpc.insecure_server_credentials()
        server_creds = grpc.xds_server_credentials(server_fallback_creds)
        server.add_secure_port(listen_address, server_creds)


def _run(port: int, maintenance_port: int, secure_mode: bool,
         server_id: str) -> None:
    if port == maintenance_port:
        server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=_THREAD_POOL_SIZE))
        _configure_test_server(server, port, secure_mode, server_id)
        _configure_maintenance_server(server, maintenance_port)
        server.start()
        logger.info("Test server listening on port %d", port)
        logger.info("Maintenance server listening on port %d", maintenance_port)
        server.wait_for_termination()
    else:
        test_server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=_THREAD_POOL_SIZE),
            xds=secure_mode)
        _configure_test_server(test_server, port, secure_mode, server_id)
        test_server.start()
        logger.info("Test server listening on port %d", port)
        maintenance_server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=_THREAD_POOL_SIZE))
        _configure_maintenance_server(maintenance_server, maintenance_port)
        maintenance_server.start()
        logger.info("Maintenance server listening on port %d", maintenance_port)
        test_server.wait_for_termination()
        maintenance_server.wait_for_termination()


def bool_arg(arg: str) -> bool:
    if arg.lower() in ("true", "yes", "y"):
        return True
    elif arg.lower() in ("false", "no", "n"):
        return False
    else:
        raise argparse.ArgumentTypeError(f"Could not parse '{arg}' as a bool.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Run Python xDS interop server.")
    parser.add_argument("--port",
                        type=int,
                        default=8080,
                        help="Port for test server.")
    parser.add_argument("--maintenance_port",
                        type=int,
                        default=8080,
                        help="Port for servers besides test server.")
    parser.add_argument(
        "--secure_mode",
        type=bool_arg,
        default="False",
        help="If specified, uses xDS to retrieve server credentials.")
    parser.add_argument("--server_id",
                        type=str,
                        default="python_server",
                        help="The server ID to return in responses..")
    parser.add_argument('--verbose',
                        help='verbose log output',
                        default=False,
                        action='store_true')
    args = parser.parse_args()
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)
    if args.secure_mode and args.port == args.maintenance_port:
        raise ValueError(
            "--port and --maintenance_port must not be the same when --secure_mode is set."
        )
    _run(args.port, args.maintenance_port, args.secure_mode, args.server_id)
