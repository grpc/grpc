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
"""The Python implementation of the GRPC hellostreamingworld.MultiGreeter server."""

import logging
from concurrent import futures

import grpc

import hellostreamingworld_pb2
import hellostreamingworld_pb2_grpc

def _wrap_handler(handler, behavior_wrapper):
    factory = grpc.unary_stream_rpc_method_handler
    behavior = handler.unary_stream
    return factory(
        behavior_wrapper(behavior),
        request_deserializer=handler.request_deserializer,
        response_serializer=handler.response_serializer,
    )


def _wrap_request_iterator(interceptor_id, request_iterator):
    for i, request in enumerate(request_iterator):
        logging.info(f"{interceptor_id}: before request {i} sent")
        yield request
        logging.info(f"{interceptor_id}: after request {i} sent")


def _wrap_response_iterator(interceptor_id, response_iterator):
    for i, response in enumerate(response_iterator):
        logging.info(f"{interceptor_id}: before response {i} returned")
        yield response
        logging.info(f"{interceptor_id}: after response {i} returned")
    logging.info(f"{interceptor_id}: after RPC")


class ServerInterceptor(grpc.ServerInterceptor):

    def __init__(self, interceptor_id: str):
        self._interceptor_id = interceptor_id

    def intercept_service(self, continuation, handler_call_details):
        def wrap_behavior(behavior):
            def _unary_stream(request, context):
                logging.info(f"{self._interceptor_id}: before RPC")
                response_iterator = behavior(request, context)
                new_response_iterator = _wrap_response_iterator(self._interceptor_id, response_iterator)
                return new_response_iterator

            return _unary_stream

        handler = continuation(handler_call_details)
        return _wrap_handler(handler, wrap_behavior)


class MultiGreeter(hellostreamingworld_pb2_grpc.MultiGreeterServicer):
    def sayHello(self, request, context):
        num_greetings = int(request.num_greetings)
        for i in range(num_greetings):
            logging.info(f"SayHelloServerStreaming: received request {i}")
            yield hellostreamingworld_pb2.HelloReply(message=f"Hello number {i}, {request.name}!")


def serve():
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=1),
        interceptors=[
            ServerInterceptor("interceptor1"),
            ServerInterceptor("interceptor2"),
            ServerInterceptor("interceptor3"),
        ],
    )
    hellostreamingworld_pb2_grpc.add_MultiGreeterServicer_to_server(MultiGreeter(), server)
    listen_addr = "[::]:50051"
    server.add_insecure_port(listen_addr)
    server.start()
    server.wait_for_termination()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    serve()
