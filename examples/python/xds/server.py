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

from concurrent import futures
import argparse
import logging
import multiprocessing
import socket

import grpc

import helloworld_pb2
import helloworld_pb2_grpc

from grpc_reflection.v1alpha import reflection
from grpc_health.v1 import health
from grpc_health.v1 import health_pb2
from grpc_health.v1 import health_pb2_grpc

_DESCRIPTION = "A general purpose dummy server."


class Greeter(helloworld_pb2_grpc.GreeterServicer):

    def __init__(self, hostname: str):
        self._hostname = hostname if hostname else socket.gethostname()

    def SayHello(self, request: helloworld_pb2.HelloRequest,
                 context: grpc.ServicerContext) -> helloworld_pb2.HelloReply:
        return helloworld_pb2.HelloReply(
            message=f"Hello {request.name} from {self._hostname}!")


def serve(port: int, hostname: str):
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=multiprocessing.cpu_count()))

    # Add the application servicer to the server.
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(hostname), server)

    # Create a health check servicer. We use the non-blocking implementation
    # to avoid thread starvation.
    health_servicer = health.HealthServicer(
        experimental_non_blocking=True,
        experimental_thread_pool=futures.ThreadPoolExecutor(max_workers=1))
    health_pb2_grpc.add_HealthServicer_to_server(health_servicer, server)

    # Create a tuple of all of the services we want to export via reflection.
    services = tuple(
        service.full_name
        for service in helloworld_pb2.DESCRIPTOR.services_by_name.values()) + (
            reflection.SERVICE_NAME, health.SERVICE_NAME)

    # Add the reflection service to the server.
    reflection.enable_server_reflection(services, server)
    server.add_insecure_port(f"[::]:{port}")
    server.start()

    # Mark all services as healthy.
    overall_server_health = ""
    for service in services + (overall_server_health,):
        health_servicer.set(service, health_pb2.HealthCheckResponse.SERVING)

    # Park the main application thread.
    server.wait_for_termination()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument("port",
                        default=50051,
                        type=int,
                        nargs="?",
                        help="The port on which to listen.")
    parser.add_argument("hostname",
                        type=str,
                        default=None,
                        nargs="?",
                        help="The name clients will see in responses.")
    args = parser.parse_args()
    logging.basicConfig()
    serve(args.port, args.hostname)
