# Copyright 2018 The gRPC Authors
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
"""Example gRPC client that gets/sets metadata (HTTP2 headers)"""

from __future__ import print_function

import logging

import grpc
import route_guide_pb2
import route_guide_pb2_grpc


def generate_messages():
    messages = [
        make_route_note("First message", 0, 0),
        make_route_note("Second message", 0, 1),
        make_route_note("Third message", 1, 0),
        make_route_note("Fourth message", 0, 0),
        make_route_note("Fifth message", 1, 0),
    ]
    for msg in messages:
        print("Sending %s at %s" % (msg.message, format_point(msg.location)))
        yield msg


def format_point(point):
    # not delegating in point.__str__ because it is an empty string when its
    # values are zero. In addition, it puts a newline between the fields.
    return "latitude: %d, longitude: %d" % (point.latitude, point.longitude)


def run():
    # NOTE(gRPC Python Team): .close() is possible on a channel and should be
    # used in circumstances in which the with statement does not fit the needs
    # of the code.
    with grpc.insecure_channel("localhost:50051") as channel:
        stub = route_guide_pb2_grpc.RouteGuideStub(channel)
        responses, call = stub.RouteChat.with_call(
            generate_messages(),
            metadata=(
                ("initial-metadata-1", "The value must be str"),
                (
                    "binary-metadata-bin",
                    b"With -bin surffix, the value must be bytes",
                ),
                ("accesstoken", "gRPC Python is great"),
            ),
        )

    for response in responses:
        print(
            "Received message %s at %s"
            % (response.message, format_point(response.location))
        )

    for key, value in call.trailing_metadata():
        print(
            "Greeter client received trailing metadata: key=%s value=%s"
            % (key, value)
        )


if __name__ == "__main__":
    logging.basicConfig()
    run()
