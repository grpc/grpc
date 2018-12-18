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
"""Example gRPC server that utilizes termination callbacks"""

from __future__ import print_function
from concurrent import futures
import time
import logging

import grpc

from hellostreamingworld_pb2 import HelloReply
import hellostreamingworld_pb2_grpc

_ONE_DAY_IN_SECONDS = 60 * 60 * 24


class MultiGreeter(hellostreamingworld_pb2_grpc.MultiGreeterServicer):

    def sayHello(self, request, context):

        def on_termination():
            print('Greeting to %s ends' % request.name)

        # The callback will be executed when the RPC ends, even if
        # the RPC is aborted.
        context.add_callback(on_termination)

        num_greetings = int(request.num_greetings)
        if num_greetings < 0:
            context.abort(grpc.StatusCode.INVALID_ARGUMENT,
                          'Number of greetings cannot be negative')

        for i in range(num_greetings):
            yield HelloReply(message='No. %s Greeting: Hello, %s!' % (
                i + 1, request.name))
            time.sleep(1)


def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    hellostreamingworld_pb2_grpc.add_MultiGreeterServicer_to_server(
        MultiGreeter(), server)
    server.add_insecure_port('[::]:50051')
    server.start()
    try:
        while True:
            time.sleep(_ONE_DAY_IN_SECONDS)
    except KeyboardInterrupt:
        server.stop(0)


if __name__ == '__main__':
    logging.basicConfig()
    serve()
