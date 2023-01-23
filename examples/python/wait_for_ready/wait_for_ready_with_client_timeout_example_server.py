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
"""The Python serer of utilizing wait-for-ready flag with client time out."""

from concurrent import futures
from time import sleep
import logging

import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto")

_INITIAL_METADATA = ((b'initial-md', 'initial-md-value'),)

class Greeter(helloworld_pb2_grpc.GreeterServicer):
    def SayHelloStreamReply(self, request, servicer_context):
        # Send initial metadata back to indicate server is ready and running.
        print("sleeping 5s before sending metadata back")
        sleep(5)

        # Initial metadata will be send back immediately after calling send_initial_metadata.
        print("sending inital metadata back")
        servicer_context.send_initial_metadata(_INITIAL_METADATA)

        # Server can do whatever it wants here before send actual response.
        yield helloworld_pb2.HelloReply(message='Hello, %s!' % (request.name))

def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    server.add_insecure_port('[::]:50051')
    print("starting server")
    server.start()
    server.wait_for_termination()


if __name__ == '__main__':
    logging.basicConfig()
    serve()
