# Copyright 2023 The gRPC Authors
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
"""Server of the Python example of TLS."""

from concurrent import futures
import logging

import _credentials
import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)

_PORT = 50051
_LISTEN_ADDRESS_TEMPLATE = "localhost:%d"


class SimpleGreeter(helloworld_pb2_grpc.GreeterServicer):
    def SayHello(self, request, unused_context):
        return helloworld_pb2.HelloReply(message="Hello, %s!" % request.name)


def serve():
    server = grpc.server(futures.ThreadPoolExecutor())
    helloworld_pb2_grpc.add_GreeterServicer_to_server(SimpleGreeter(), server)

    # Loading credentials
    server_credentials = grpc.ssl_server_credentials(
        (
            (
                _credentials.SERVER_CERTIFICATE_KEY,
                _credentials.SERVER_CERTIFICATE,
            ),
        )
    )

    # Pass down credentials
    server.add_secure_port(_LISTEN_ADDRESS_TEMPLATE % _PORT, server_credentials)

    server.start()
    logging.info("Server is listening at port :%d", _PORT)
    server.wait_for_termination()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    serve()
