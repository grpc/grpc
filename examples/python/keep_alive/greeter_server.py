# Copyright 2023 gRPC authors.
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
"""The Python implementation of the GRPC helloworld.Greeter server with keepAlive options."""

from concurrent import futures
import logging
from time import sleep

import grpc
import helloworld_pb2
import helloworld_pb2_grpc


class Greeter(helloworld_pb2_grpc.GreeterServicer):
    def SayHello(self, request, context):
        message = request.name
        if message.startswith("[delay]"):
            sleep(5)
        return helloworld_pb2.HelloReply(message=message)


def serve():
    """
    grpc.keepalive_time_ms: The period (in milliseconds) after which a keepalive ping is
        sent on the transport.
    grpc.keepalive_timeout_ms: The amount of time (in milliseconds) the sender of the keepalive
        ping waits for an acknowledgement. If it does not receive an acknowledgment within
        this time, it will close the connection.
    grpc.http2.min_ping_interval_without_data_ms: Minimum allowed time (in milliseconds)
        between a server receiving successive ping frames without sending any data/header frame.
    grpc.max_connection_idle_ms: Maximum time (in milliseconds) that a channel may have no
        outstanding rpcs, after which the server will close the connection.
    grpc.max_connection_age_ms: Maximum time (in milliseconds) that a channel may exist.
    grpc.max_connection_age_grace_ms: Grace period (in milliseconds) after the channel
        reaches its max age.
    grpc.http2.max_pings_without_data: How many pings can the client send before needing to
        send a data/header frame.
    grpc.keepalive_permit_without_calls: If set to 1 (0 : false; 1 : true), allows keepalive
        pings to be sent even if there are no calls in flight.
    For more details, check: https://github.com/grpc/grpc/blob/master/doc/keepalive.md
    """
    server_options = [
        ("grpc.keepalive_time_ms", 20000),
        ("grpc.keepalive_timeout_ms", 10000),
        ("grpc.http2.min_ping_interval_without_data_ms", 5000),
        ("grpc.max_connection_idle_ms", 10000),
        ("grpc.max_connection_age_ms", 30000),
        ("grpc.max_connection_age_grace_ms", 5000),
        ("grpc.http2.max_pings_without_data", 5),
        ("grpc.keepalive_permit_without_calls", 1),
    ]
    port = "50051"
    server = grpc.server(
        thread_pool=futures.ThreadPoolExecutor(max_workers=10),
        options=server_options,
    )
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    server.add_insecure_port("[::]:" + port)
    server.start()
    print("Server started, listening on " + port)
    server.wait_for_termination()


if __name__ == "__main__":
    logging.basicConfig()
    serve()
