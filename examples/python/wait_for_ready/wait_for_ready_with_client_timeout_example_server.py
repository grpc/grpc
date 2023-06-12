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
"""An example of setting a server connection timeout independent from the
overall RPC timeout.

For stream server, if client set wait_for_ready but server never actually starts,
client will wait indefinitely, this example will do the following steps to set a
timeout on client side:
1. Set server to return initial_metadata once it receives request.
2. Client will set a timer (customized client timeout) waiting for initial_metadata.
3. Client will timeout if it didn't receive initial_metadata.
"""

from concurrent import futures
import logging
from time import sleep

import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)

_INITIAL_METADATA = ((b"initial-md", "initial-md-value"),)


def starting_up_server():
    print("sleeping 5s before sending metadata back")
    sleep(5)


def do_work():
    print("server is processing the request")
    sleep(5)


class Greeter(helloworld_pb2_grpc.GreeterServicer):
    def SayHelloStreamReply(self, request, servicer_context):
        # Suppose server will take some time to setup, client can set the time it willing to wait
        # for server to up and running.
        starting_up_server()

        # Initial metadata will be send back immediately after calling send_initial_metadata.
        print("sending inital metadata back")
        servicer_context.send_initial_metadata(_INITIAL_METADATA)

        # Time for server to process the request.
        do_work()

        # Sending actual response.
        for i in range(3):
            yield helloworld_pb2.HelloReply(
                message="Hello %s times %s" % (request.name, i)
            )


def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    server.add_insecure_port("[::]:50051")
    print("starting server")
    server.start()
    server.wait_for_termination()


if __name__ == "__main__":
    logging.basicConfig()
    serve()
