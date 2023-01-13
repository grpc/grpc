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

from typing import Any, Optional, Tuple

from google.protobuf import descriptor  # pytype: disable=pyi-error
import grpc
from grpc._typing import MetadataType
import grpc_testing
from grpc_testing._channel import _channel_state
from grpc_testing._channel import _rpc_state


class _UnaryUnary(grpc_testing.UnaryUnaryChannelRpc):
    _rpc_state: _rpc_state.State

    def __init__(self, rpc_state: _rpc_state.State):
        self._rpc_state = rpc_state

    def send_initial_metadata(self,
                              initial_metadata: Optional[MetadataType]) -> None:
        self._rpc_state.send_initial_metadata(initial_metadata)

    def cancelled(self) -> bool:
        self._rpc_state.cancelled()

    def terminate(self, response: Any, trailing_metadata: MetadataType,
                  code: grpc.StatusCode, details: str) -> None:
        self._rpc_state.terminate_with_response(response, trailing_metadata,
                                                code, details)


class _UnaryStream(grpc_testing.UnaryStreamChannelRpc):
    _rpc_state: _rpc_state.State

    def __init__(self, rpc_state: _rpc_state.State):
        self._rpc_state = rpc_state

    def send_initial_metadata(self,
                              initial_metadata: Optional[MetadataType]) -> None:
        self._rpc_state.send_initial_metadata(initial_metadata)

    def send_response(self, response: Any) -> None:
        self._rpc_state.send_response(response)

    def cancelled(self) -> bool:
        self._rpc_state.cancelled()

    def terminate(self, trailing_metadata: MetadataType, code: grpc.StatusCode,
                  details: str) -> None:
        self._rpc_state.terminate(trailing_metadata, code, details)


class _StreamUnary(grpc_testing.StreamUnaryChannelRpc):
    _rpc_state: _rpc_state.State

    def __init__(self, rpc_state: _rpc_state.State):
        self._rpc_state = rpc_state

    def send_initial_metadata(self,
                              initial_metadata: Optional[MetadataType]) -> None:
        self._rpc_state.send_initial_metadata(initial_metadata)

    def take_request(self) -> Any:
        return self._rpc_state.take_request()

    def requests_closed(self) -> bool:
        return self._rpc_state.requests_closed()

    def cancelled(self) -> bool:
        self._rpc_state.cancelled()

    def terminate(self, response: Any, trailing_metadata: MetadataType,
                  code: grpc.StatusCode, details: str) -> None:
        self._rpc_state.terminate_with_response(response, trailing_metadata,
                                                code, details)


class _StreamStream(grpc_testing.StreamStreamChannelRpc):
    _rpc_state: _rpc_state.State

    def __init__(self, rpc_state: _rpc_state.State):
        self._rpc_state = rpc_state

    def send_initial_metadata(self,
                              initial_metadata: Optional[MetadataType]) -> None:
        self._rpc_state.send_initial_metadata(initial_metadata)

    def take_request(self) -> Any:
        return self._rpc_state.take_request()

    def send_response(self, response: Any) -> None:
        self._rpc_state.send_response(response)

    def requests_closed(self) -> bool:
        return self._rpc_state.requests_closed()

    def cancelled(self) -> bool:
        self._rpc_state.cancelled()

    def terminate(self, trailing_metadata: MetadataType, code: grpc.StatusCode,
                  details: str):
        self._rpc_state.terminate(trailing_metadata, code, details)


def unary_unary(
    channel_state: _channel_state.State,
    method_descriptor: descriptor.MethodDescriptor
) -> Tuple[Optional[MetadataType], Any, _UnaryUnary]:
    rpc_state = channel_state.take_rpc_state(method_descriptor)
    invocation_metadata, request = (
        rpc_state.take_invocation_metadata_and_request())
    return invocation_metadata, request, _UnaryUnary(rpc_state)


def unary_stream(
    channel_state: _channel_state.State,
    method_descriptor: descriptor.MethodDescriptor
) -> Tuple[Optional[MetadataType], Any, _UnaryStream]:
    rpc_state = channel_state.take_rpc_state(method_descriptor)
    invocation_metadata, request = (
        rpc_state.take_invocation_metadata_and_request())
    return invocation_metadata, request, _UnaryStream(rpc_state)


def stream_unary(
    channel_state: _channel_state.State,
    method_descriptor: descriptor.MethodDescriptor
) -> Tuple[Optional[MetadataType], _StreamUnary]:
    rpc_state = channel_state.take_rpc_state(method_descriptor)
    return rpc_state.take_invocation_metadata(), _StreamUnary(rpc_state)


def stream_stream(
    channel_state: _channel_state.State,
    method_descriptor: descriptor.MethodDescriptor
) -> Tuple[Optional[MetadataType], _StreamStream]:
    rpc_state = channel_state.take_rpc_state(method_descriptor)
    return rpc_state.take_invocation_metadata(), _StreamStream(rpc_state)
