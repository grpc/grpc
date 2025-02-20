# Copyright 2020 The gRPC authors.
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
"""The Python implementation of the GRPC helloworld.Greeter server."""

import argparse
from concurrent import futures
import logging
import socket

import grpc
from grpc_health.v1 import health
from grpc_health.v1 import health_pb2
from grpc_health.v1 import health_pb2_grpc
from grpc_reflection.v1alpha import reflection
import helloworld_pb2
import helloworld_pb2_grpc

_DESCRIPTION = "A general purpose phony server."

_LISTEN_HOST = "0.0.0.0"

_THREAD_POOL_SIZE = 256

logger = logging.getLogger()
console_handler = logging.StreamHandler()
formatter = logging.Formatter(fmt="%(asctime)s: %(levelname)-8s %(message)s")
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)


class Greeter(helloworld_pb2_grpc.GreeterServicer):
    def __init__(self, hostname: str):
        self._hostname = hostname if hostname else socket.gethostname()

    def SayHello(
        self,
        request: helloworld_pb2.HelloRequest,
        context: grpc.ServicerContext,
    ) -> helloworld_pb2.HelloReply:
        return helloworld_pb2.HelloReply(
            message=f"Hello {request.name} from {self._hostname}!"
        )


def _configure_maintenance_server(
    server: grpc.Server, maintenance_port: int
) -> None:
    listen_address = f"{_LISTEN_HOST}:{maintenance_port}"
    server.add_insecure_port(listen_address)

    # Create a health check servicer. We use the non-blocking implementation
    # to avoid thread starvation.
    health_servicer = health.HealthServicer(
        experimental_non_blocking=True,
        experimental_thread_pool=futures.ThreadPoolExecutor(
            max_workers=_THREAD_POOL_SIZE
        ),
    )

    # Create a tuple of all of the services we want to export via reflection.
    services = tuple(
        service.full_name
        for service in helloworld_pb2.DESCRIPTOR.services_by_name.values()
    ) + (reflection.SERVICE_NAME, health.SERVICE_NAME)

    # Mark all services as healthy.
    health_pb2_grpc.add_HealthServicer_to_server(health_servicer, server)
    for service in services:
        health_servicer.set(service, health_pb2.HealthCheckResponse.SERVING)
    reflection.enable_server_reflection(services, server)


def _configure_greeter_server(
    server: grpc.Server, port: int, secure_mode: bool, hostname
) -> None:
    # Add the application servicer to the server.
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(hostname), server)
    listen_address = f"{_LISTEN_HOST}:{port}"
    if not secure_mode:
        server.add_insecure_port(listen_address)
    else:
        # Use xDS credentials.
        logger.info("Running with xDS Server credentials")

        # Fall back to insecure credentials.
        server_fallback_creds = grpc.insecure_server_credentials()
        server_creds = grpc.xds_server_credentials(server_fallback_creds)
        server.add_secure_port(listen_address, server_creds)


def serve(
    port: int, hostname: str, maintenance_port: int, secure_mode: bool
) -> None:
    if port == maintenance_port:
        # If maintenance port and port are the same, start a single server.
        server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=_THREAD_POOL_SIZE)
        )
        _configure_greeter_server(server, port, secure_mode, hostname)
        _configure_maintenance_server(server, maintenance_port)
        server.start()
        logger.info("Greeter server listening on port %d", port)
        logger.info("Maintenance server listening on port %d", maintenance_port)
        server.wait_for_termination()
    else:
        # Otherwise, start two different servers.
        greeter_server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=_THREAD_POOL_SIZE),
            xds=secure_mode,
        )
        _configure_greeter_server(greeter_server, port, secure_mode, hostname)
        greeter_server.start()
        logger.info("Greeter server listening on port %d", port)
        maintenance_server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=_THREAD_POOL_SIZE)
        )
        _configure_maintenance_server(maintenance_server, maintenance_port)
        maintenance_server.start()
        logger.info("Maintenance server listening on port %d", maintenance_port)
        greeter_server.wait_for_termination()
        maintenance_server.wait_for_termination()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument(
        "port",
        default=50051,
        type=int,
        nargs="?",
        help="The port on which to listen.",
    )
    parser.add_argument(
        "hostname",
        type=str,
        default=None,
        nargs="?",
        help="The name clients will see in responses.",
    )
    parser.add_argument(
        "--xds-creds",
        action="store_true",
        help="If specified, uses xDS credentials to connect to the server.",
    )
    args = parser.parse_args()
    logging.basicConfig()
    logger.setLevel(logging.INFO)
    serve(args.port, args.hostname, args.port + 1, args.xds_creds)
