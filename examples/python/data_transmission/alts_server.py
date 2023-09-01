# Copyright 2020 gRPC authors.
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
"""The example of using ALTS credentials to setup gRPC server in python.

The example would only successfully run in GCP environment."""

from concurrent import futures

import grpc

import demo_pb2_grpc
from server import DemoServer

SERVER_ADDRESS = "localhost:23333"


def main():
    svr = grpc.server(futures.ThreadPoolExecutor())
    demo_pb2_grpc.add_GRPCDemoServicer_to_server(DemoServer(), svr)
    svr.add_secure_port(
        SERVER_ADDRESS, server_credentials=grpc.alts_server_credentials()
    )
    print("------------------start Python GRPC server with ALTS encryption")
    svr.start()
    svr.wait_for_termination()


if __name__ == "__main__":
    main()
