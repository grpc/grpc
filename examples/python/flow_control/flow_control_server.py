# Copyright 2015 gRPC authors.
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
"""The Python implementation of the GRPC flow control server."""

from concurrent import futures
import logging

import grpc
import flow_control_pb2
import flow_control_pb2_grpc

import time


class FlowControlServicer(flow_control_pb2_grpc.FlowControlServicer):
    def BidiStreamingCall(self, request_iterator, unused_context):
        # Delay read on server side to apply back-pressure on client
        time.sleep(5)

        bytes_received = 0

        # Read request from client on every iteration
        for i, request in enumerate(request_iterator, start=1):
            bytes_received += len(request.message)
            if (i % 10) == 0:
                print(f"Request {i}: Received {bytes_received} bytes in total")

            # Simulate server "work"
            time.sleep(2)

            # Send a response
            msg = f"Hello!"
            yield flow_control_pb2.Reply(message=msg)

            if (i % 10) == 0:
                print(f"Request {i}: Sent {bytes_received} bytes in total\n")


def serve():
    port = "50051"
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=10),
        # Setting server options to minimal, to allow low number of maximum
        # bytes in the window.
        # `bdp_probe` is set to 0(false) to disable resizing of window
        options=[
            ("grpc.http2.max_frame_size", 16384),
            ("grpc.http2.bdp_probe", 0),
            ("grpc.max_concurrent_streams", 1),
        ],
    )
    flow_control_pb2_grpc.add_FlowControlServicer_to_server(
        FlowControlServicer(), server
    )

    server.add_insecure_port("[::]:" + port)
    server.start()
    print("Server started, listening on " + port)
    server.wait_for_termination()


if __name__ == "__main__":
    logging.basicConfig()
    serve()
