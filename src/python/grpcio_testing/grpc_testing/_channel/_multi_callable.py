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

import grpc
from grpc_testing import _common
from grpc_testing._channel import _invocation


# All per-call credentials parameters are unused by this test infrastructure.
# pylint: disable=unused-argument
class UnaryUnary(grpc.UnaryUnaryMultiCallable):
    def __init__(self, method_full_rpc_name, channel_handler):
        self._method_full_rpc_name = method_full_rpc_name
        self._channel_handler = channel_handler

    def __call__(self, request, timeout=None, metadata=None, credentials=None):
        rpc_handler = self._channel_handler.invoke_rpc(
            self._method_full_rpc_name,
            _common.fuss_with_metadata(metadata),
            [request],
            True,
            timeout,
        )
        return _invocation.blocking_unary_response(rpc_handler)

    def with_call(self, request, timeout=None, metadata=None, credentials=None):
        rpc_handler = self._channel_handler.invoke_rpc(
            self._method_full_rpc_name,
            _common.fuss_with_metadata(metadata),
            [request],
            True,
            timeout,
        )
        return _invocation.blocking_unary_response_with_call(rpc_handler)

    def future(self, request, timeout=None, metadata=None, credentials=None):
        rpc_handler = self._channel_handler.invoke_rpc(
            self._method_full_rpc_name,
            _common.fuss_with_metadata(metadata),
            [request],
            True,
            timeout,
        )
        return _invocation.future_call(rpc_handler)


class UnaryStream(grpc.StreamStreamMultiCallable):
    def __init__(self, method_full_rpc_name, channel_handler):
        self._method_full_rpc_name = method_full_rpc_name
        self._channel_handler = channel_handler

    def __call__(self, request, timeout=None, metadata=None, credentials=None):
        rpc_handler = self._channel_handler.invoke_rpc(
            self._method_full_rpc_name,
            _common.fuss_with_metadata(metadata),
            [request],
            True,
            timeout,
        )
        return _invocation.ResponseIteratorCall(rpc_handler)


class StreamUnary(grpc.StreamUnaryMultiCallable):
    def __init__(self, method_full_rpc_name, channel_handler):
        self._method_full_rpc_name = method_full_rpc_name
        self._channel_handler = channel_handler

    def __call__(
        self, request_iterator, timeout=None, metadata=None, credentials=None
    ):
        rpc_handler = self._channel_handler.invoke_rpc(
            self._method_full_rpc_name,
            _common.fuss_with_metadata(metadata),
            [],
            False,
            timeout,
        )
        _invocation.consume_requests(request_iterator, rpc_handler)
        return _invocation.blocking_unary_response(rpc_handler)

    def with_call(
        self, request_iterator, timeout=None, metadata=None, credentials=None
    ):
        rpc_handler = self._channel_handler.invoke_rpc(
            self._method_full_rpc_name,
            _common.fuss_with_metadata(metadata),
            [],
            False,
            timeout,
        )
        _invocation.consume_requests(request_iterator, rpc_handler)
        return _invocation.blocking_unary_response_with_call(rpc_handler)

    def future(
        self, request_iterator, timeout=None, metadata=None, credentials=None
    ):
        rpc_handler = self._channel_handler.invoke_rpc(
            self._method_full_rpc_name,
            _common.fuss_with_metadata(metadata),
            [],
            False,
            timeout,
        )
        _invocation.consume_requests(request_iterator, rpc_handler)
        return _invocation.future_call(rpc_handler)


class StreamStream(grpc.StreamStreamMultiCallable):
    def __init__(self, method_full_rpc_name, channel_handler):
        self._method_full_rpc_name = method_full_rpc_name
        self._channel_handler = channel_handler

    def __call__(
        self, request_iterator, timeout=None, metadata=None, credentials=None
    ):
        rpc_handler = self._channel_handler.invoke_rpc(
            self._method_full_rpc_name,
            _common.fuss_with_metadata(metadata),
            [],
            False,
            timeout,
        )
        _invocation.consume_requests(request_iterator, rpc_handler)
        return _invocation.ResponseIteratorCall(rpc_handler)


# pylint: enable=unused-argument
