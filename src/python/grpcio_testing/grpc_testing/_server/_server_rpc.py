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

import grpc_testing


class UnaryUnaryServerRpc(grpc_testing.UnaryUnaryServerRpc):
    def __init__(self, handler):
        self._handler = handler

    def initial_metadata(self):
        return self._handler.initial_metadata()

    def cancel(self):
        self._handler.cancel()

    def termination(self):
        return self._handler.unary_response_termination()


class UnaryStreamServerRpc(grpc_testing.UnaryStreamServerRpc):
    def __init__(self, handler):
        self._handler = handler

    def initial_metadata(self):
        return self._handler.initial_metadata()

    def take_response(self):
        return self._handler.take_response()

    def cancel(self):
        self._handler.cancel()

    def termination(self):
        return self._handler.stream_response_termination()


class StreamUnaryServerRpc(grpc_testing.StreamUnaryServerRpc):
    def __init__(self, handler):
        self._handler = handler

    def initial_metadata(self):
        return self._handler.initial_metadata()

    def send_request(self, request):
        self._handler.add_request(request)

    def requests_closed(self):
        self._handler.requests_closed()

    def cancel(self):
        self._handler.cancel()

    def termination(self):
        return self._handler.unary_response_termination()


class StreamStreamServerRpc(grpc_testing.StreamStreamServerRpc):
    def __init__(self, handler):
        self._handler = handler

    def initial_metadata(self):
        return self._handler.initial_metadata()

    def send_request(self, request):
        self._handler.add_request(request)

    def requests_closed(self):
        self._handler.requests_closed()

    def take_response(self):
        return self._handler.take_response()

    def cancel(self):
        self._handler.cancel()

    def termination(self):
        return self._handler.stream_response_termination()
