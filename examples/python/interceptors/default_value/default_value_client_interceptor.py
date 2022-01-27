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
"""Interceptor that adds headers to outgoing requests."""

import grpc


class _ConcreteValue(grpc.Future):

    def __init__(self, result):
        self._result = result

    def cancel(self):
        return False

    def cancelled(self):
        return False

    def running(self):
        return False

    def done(self):
        return True

    def result(self, timeout=None):
        return self._result

    def exception(self, timeout=None):
        return None

    def traceback(self, timeout=None):
        return None

    def add_done_callback(self, fn):
        fn(self._result)


class DefaultValueClientInterceptor(grpc.UnaryUnaryClientInterceptor,
                                    grpc.StreamUnaryClientInterceptor):

    def __init__(self, value):
        self._default = _ConcreteValue(value)

    def _intercept_call(self, continuation, client_call_details,
                        request_or_iterator):
        response = continuation(client_call_details, request_or_iterator)
        return self._default if response.exception() else response

    def intercept_unary_unary(self, continuation, client_call_details, request):
        return self._intercept_call(continuation, client_call_details, request)

    def intercept_stream_unary(self, continuation, client_call_details,
                               request_iterator):
        return self._intercept_call(continuation, client_call_details,
                                    request_iterator)
