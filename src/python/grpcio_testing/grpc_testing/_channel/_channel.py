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
from grpc_testing._channel import _channel_rpc
from grpc_testing._channel import _multi_callable


# All serializer and deserializer parameters are not (yet) used by this
# test infrastructure.
# pylint: disable=unused-argument
class TestingChannel(grpc_testing.Channel):
    def __init__(self, time, state):
        self._time = time
        self._state = state

    def subscribe(self, callback, try_to_connect=False):
        raise NotImplementedError()

    def unsubscribe(self, callback):
        raise NotImplementedError()

    def _get_registered_call_handle(self, method: str) -> int:
        pass

    def unary_unary(
        self,
        method,
        request_serializer=None,
        response_deserializer=None,
        _registered_method=False,
    ):
        return _multi_callable.UnaryUnary(method, self._state)

    def unary_stream(
        self,
        method,
        request_serializer=None,
        response_deserializer=None,
        _registered_method=False,
    ):
        return _multi_callable.UnaryStream(method, self._state)

    def stream_unary(
        self,
        method,
        request_serializer=None,
        response_deserializer=None,
        _registered_method=False,
    ):
        return _multi_callable.StreamUnary(method, self._state)

    def stream_stream(
        self,
        method,
        request_serializer=None,
        response_deserializer=None,
        _registered_method=False,
    ):
        return _multi_callable.StreamStream(method, self._state)

    def _close(self):
        # TODO(https://github.com/grpc/grpc/issues/12531): Decide what
        # action to take here, if any?
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._close()
        return False

    def close(self):
        self._close()

    def take_unary_unary(self, method_descriptor):
        return _channel_rpc.unary_unary(self._state, method_descriptor)

    def take_unary_stream(self, method_descriptor):
        return _channel_rpc.unary_stream(self._state, method_descriptor)

    def take_stream_unary(self, method_descriptor):
        return _channel_rpc.stream_unary(self._state, method_descriptor)

    def take_stream_stream(self, method_descriptor):
        return _channel_rpc.stream_stream(self._state, method_descriptor)


# pylint: enable=unused-argument
