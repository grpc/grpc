# Copyright 2024 gRPC authors.
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
"""Example gRPC client to depict flow control in gRPC"""

import logging

import grpc
import helloworld_pb2
import helloworld_pb2_grpc
import helpers


def get_iter_data():
    max_iter = 100
    data_size = 2000
    test_request_data = bytes("1" * data_size, "utf-8")

    for i in range(1, (max_iter + 1)):
        if (i % 10) == 0:
            print(
                f"\n{helpers.get_current_time()}   "
                f"Request {i}: Sent {(data_size*i)} bytes in total"
            )

        yield helloworld_pb2.HelloRequest(name=test_request_data)


def run():
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
        stub = helloworld_pb2_grpc.GreeterStub(channel)

        responses = stub.SayHelloBidiStream(get_iter_data())
        for i, _ in enumerate(responses, start=1):
            if (i % 10) == 0:
                print(
                    f"{helpers.get_current_time()}   "
                    f"Received {i} responses\n"
                )


if __name__ == "__main__":
    logging.basicConfig()
    run()
