# Copyright 2017 gRPC authors.
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
"""Interceptor that ensures a specific header is present."""

import grpc


def _unary_unary_rpc_terminator(code, details):
    def terminate(ignored_request, context):
        context.abort(code, details)

    return grpc.unary_unary_rpc_method_handler(terminate)


class RequestHeaderValidatorInterceptor(grpc.ServerInterceptor):
    def __init__(self, header, value, code, details):
        self._header = header
        self._value = value
        self._terminator = _unary_unary_rpc_terminator(code, details)

    def intercept_service(self, continuation, handler_call_details):
        if (
            self._header,
            self._value,
        ) in handler_call_details.invocation_metadata:
            return continuation(handler_call_details)
        else:
            return self._terminator
