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
"""The Python implementation of the gRPC route guide client."""

from __future__ import print_function

import logging
import random
import os

os.environ["GRPC_SINGLE_THREADED_UNARY_STREAM"] = "true"

import grpc
import route_guide_pb2
import route_guide_pb2_grpc
import route_guide_resources

import response_wrappers

class LoggingInterceptor(grpc.UnaryStreamClientInterceptor):

    def __init__(self):
        pass

    def intercept_unary_stream(self, continuation, client_call_details, request_iterator):
        response = continuation(client_call_details, request_iterator)
        return response_wrappers._UnaryStreamWrapper(response)


def guide_list_features(stub):
    rectangle = route_guide_pb2.Rectangle(
        lo=route_guide_pb2.Point(latitude=400000000, longitude=-750000000),
        hi=route_guide_pb2.Point(latitude=420000000, longitude=-730000000))
    print("Looking for features between 40, -75 and 42, -73")

    features = stub.ListFeatures(rectangle)

    for feature in features:
        print(".", end="")
    print()


def run():
    with grpc.insecure_channel('localhost:50051') as channel:
        intercepted_channel = grpc.intercept_channel(channel, LoggingInterceptor())
        stub = route_guide_pb2_grpc.RouteGuideStub(intercepted_channel)
        guide_list_features(stub)


if __name__ == '__main__':
    logging.basicConfig()
    run()
