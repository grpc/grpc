# Copyright 2023 gRPC authors.
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
"""The Python implementation of the GRPC hellostreamingworld.MultiGreeter client."""

import logging

import grpc

import hellostreamingworld_pb2
import hellostreamingworld_pb2_grpc

def _wrap_response_iterator(interceptor_id, response_iterator):
    for i, response in enumerate(response_iterator):
        logging.info(f"{interceptor_id}: before response {i} returned")
        yield response
        logging.info(f"{interceptor_id}: after response {i} returned")
    logging.info(f"{interceptor_id}: after RPC")


class UnaryStreamClientInterceptor(grpc.UnaryStreamClientInterceptor):
    def __init__(self, interceptor_id):
        self._interceptor_id = interceptor_id

    def intercept_unary_stream(self, continuation, client_call_details, request):
        logging.info(f"{self._interceptor_id}: before RPC")
        response_iterator = continuation(client_call_details, request)
        new_response_iterator = _wrap_response_iterator(self._interceptor_id, response_iterator)
        return new_response_iterator


def run() -> None:
    with grpc.insecure_channel(f"localhost:50051") as channel:
        intercept_channel = grpc.intercept_channel(
            channel,
            UnaryStreamClientInterceptor("interceptor1"),
            UnaryStreamClientInterceptor("interceptor2"),
            UnaryStreamClientInterceptor("interceptor3"),
        )
        stub = hellostreamingworld_pb2_grpc.MultiGreeterStub(intercept_channel)

        request = hellostreamingworld_pb2.HelloRequest(name="Alice", num_greetings="2")
        for response in stub.sayHello(request):
            logging.info(response.message)


if __name__ == "__main__":
    logging.basicConfig(level="INFO")
    run()
