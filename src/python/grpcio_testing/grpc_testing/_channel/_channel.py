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

from typing import Any, Callable, Optional, Tuple

from google.protobuf import descriptor  # pytype: disable=pyi-error
import grpc
from grpc._typing import DeserializingFunction
from grpc._typing import MetadataType
from grpc._typing import SerializingFunction
import grpc_testing
from grpc_testing._channel import _channel_rpc
from grpc_testing._channel import _channel_state
from grpc_testing._channel import _multi_callable


# All serializer and deserializer parameters are not (yet) used by this
# test infrastructure.
# pylint: disable=unused-argument
class TestingChannel(grpc_testing.Channel):
    _time: float
    _state: _channel_state.State

    def __init__(self, time: float, state: _channel_state.State):
        self._time = time
        self._state = state

    def subscribe(self,
                  callback: Callable[[grpc.ChannelConnectivity], None],
                  try_to_connect: bool = False) -> None:
        raise NotImplementedError()

    def unsubscribe(
            self, callback: Callable[[grpc.ChannelConnectivity], None]) -> None:
        raise NotImplementedError()

    def unary_unary(
        self,
        method: str,
        request_serializer: Optional[SerializingFunction] = None,
        response_deserializer: Optional[DeserializingFunction] = None
    ) -> grpc.UnaryUnaryMultiCallable:
        return _multi_callable.UnaryUnary(method, self._state)

    def unary_stream(
        self,
        method: str,
        request_serializer: Optional[SerializingFunction] = None,
        response_deserializer: Optional[DeserializingFunction] = None
    ) -> grpc.UnaryStreamMultiCallable:
        return _multi_callable.UnaryStream(method, self._state)

    def stream_unary(
        self,
        method: str,
        request_serializer: Optional[SerializingFunction] = None,
        response_deserializer: Optional[DeserializingFunction] = None
    ) -> grpc.StreamUnaryMultiCallable:
        return _multi_callable.StreamUnary(method, self._state)

    def stream_stream(
        self,
        method: str,
        request_serializer: Optional[SerializingFunction] = None,
        response_deserializer: Optional[DeserializingFunction] = None
    ) -> grpc.StreamStreamMultiCallable:
        return _multi_callable.StreamStream(method, self._state)

    def _close(self) -> None:
        # TODO(https://github.com/grpc/grpc/issues/12531): Decide what
        # action to take here, if any?
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._close()
        return False

    def close(self) -> None:
        self._close()

    def take_unary_unary(
        self, method_descriptor: descriptor.MethodDescriptor
    ) -> Tuple[Optional[MetadataType], Any, grpc_testing.UnaryUnaryChannelRpc]:
        return _channel_rpc.unary_unary(self._state, method_descriptor)

    def take_unary_stream(
        self, method_descriptor: descriptor.MethodDescriptor
    ) -> Tuple[Optional[MetadataType], Any, grpc_testing.UnaryStreamChannelRpc]:
        return _channel_rpc.unary_stream(self._state, method_descriptor)

    def take_stream_unary(
        self, method_descriptor: descriptor.MethodDescriptor
    ) -> Tuple[Optional[MetadataType], grpc_testing.StreamUnaryChannelRpc]:
        return _channel_rpc.stream_unary(self._state, method_descriptor)

    def take_stream_stream(
        self, method_descriptor: descriptor.MethodDescriptor
    ) -> Tuple[Optional[MetadataType], grpc_testing.StreamStreamChannelRpc]:
        return _channel_rpc.stream_stream(self._state, method_descriptor)


# pylint: enable=unused-argument
