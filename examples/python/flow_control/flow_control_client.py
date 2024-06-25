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
"""The Python implementation of the GRPC flow_control.Greeter client."""

from __future__ import print_function

import logging

import grpc
import flow_control_pb2
import flow_control_pb2_grpc


def get_iter_data():
    max_iter = 100
    data_size = 2000
    test_request_data = bytes("1" * data_size, "utf-8")

    for i in range(1, (max_iter + 1)):
        if (i % 10) == 0:
            print(f"Request {i}: Sent {(data_size*i)} bytes in total")

        yield flow_control_pb2.Request(message=test_request_data)


def run():
    # NOTE(gRPC Python Team): .close() is possible on a channel and should be
    # used in circumstances in which the with statement does not fit the needs
    # of the code.
    with grpc.insecure_channel(
        "localhost:50051",
        # Setting server options to minimal, to allow low number of maximum
        # bytes in the window
        # `bdp_probe` is set to 0(false) to disable resizing of window
        options=[
            ("grpc.http2.max_frame_size", 16384),
            ("grpc.http2.bdp_probe", 0),
        ],
    ) as channel:
        stub = flow_control_pb2_grpc.FlowControlStub(channel)

        responses = stub.BidiStreamingCall(get_iter_data())
        for i, _ in enumerate(responses, start=1):
            if (i % 10) == 0:
                print(f"Received {i} responses\n")


if __name__ == "__main__":
    logging.basicConfig()
    run()
