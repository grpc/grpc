# Copyright 2018 gRPC authors.
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
"""Interceptor that computes the latency of gRPC handlers."""

import time

import grpc


def _unary_unary_rpc_terminator(code, details):

    def terminate(ignored_request, context):
        context.abort(code, details)

    return grpc.unary_unary_rpc_method_handler(terminate)


def _wrap_rpc_behavior(handler, fn):
    if handler is None:
        return None

    if handler.request_streaming and handler.response_streaming:
        behavior_fn = handler.stream_stream
        handler_factory = grpc.stream_stream_rpc_method_handler
    elif handler.request_streaming and not handler.response_streaming:
        behavior_fn = handler.stream_unary
        handler_factory = grpc.stream_unary_rpc_method_handler
    elif not handler.request_streaming and handler.response_streaming:
        behavior_fn = handler.unary_stream
        handler_factory = grpc.unary_stream_rpc_method_handler
    else:
        behavior_fn = handler.unary_unary
        handler_factory = grpc.unary_unary_rpc_method_handler

    return handler_factory(fn(behavior_fn,
                              handler.request_streaming,
                              handler.response_streaming),
                           request_deserializer=handler.request_deserializer,
                           response_serializer=handler.response_serializer)


class ServiceLatencyInterceptor(grpc.ServerInterceptor):

    def __init__(self, collector):
        self._collector = collector

    def intercept_service(self, continuation, handler_call_details):
        def latency_wrapper(behavior, request_streaming, response_streaming):
            def new_behavior(request_or_iterator, servicer_context):
                start = time.time()
                try:
                    return behavior(request_or_iterator, servicer_context)
                finally:
                    self._collector(time.time()-start)
            return new_behavior

        return _wrap_rpc_behavior(continuation(handler_call_details), latency_wrapper)
