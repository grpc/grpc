# Copyright 2020 The gRPC Authors
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

import asyncio
from typing import AsyncIterable

import grpc
from grpc.aio._metadata import Metadata
from grpc.aio._typing import MetadataKey
from grpc.aio._typing import MetadataValue
from grpc.aio._typing import MetadatumType
from grpc.experimental import aio

from tests.unit.framework.common import test_constants

ADHOC_METHOD = "/test/AdHoc"


def seen_metadata(expected: Metadata, actual: Metadata):
    return not bool(set(tuple(expected)) - set(tuple(actual)))


def seen_metadatum(
    expected_key: MetadataKey, expected_value: MetadataValue, actual: Metadata
) -> bool:
    obtained = actual[expected_key]
    return obtained == expected_value


async def block_until_certain_state(
    channel: aio.Channel, expected_state: grpc.ChannelConnectivity
):
    state = channel.get_state()
    while state != expected_state:
        await channel.wait_for_state_change(state)
        state = channel.get_state()


def inject_callbacks(call: aio.Call):
    first_callback_ran = asyncio.Event()

    def first_callback(call):
        # Validate that all resopnses have been received
        # and the call is an end state.
        assert call.done()
        first_callback_ran.set()

    second_callback_ran = asyncio.Event()

    def second_callback(call):
        # Validate that all responses have been received
        # and the call is an end state.
        assert call.done()
        second_callback_ran.set()

    call.add_done_callback(first_callback)
    call.add_done_callback(second_callback)

    async def validation():
        await asyncio.wait_for(
            asyncio.gather(
                first_callback_ran.wait(), second_callback_ran.wait()
            ),
            test_constants.SHORT_TIMEOUT,
        )

    return validation()


class CountingRequestIterator:
    def __init__(self, request_iterator):
        self.request_cnt = 0
        self._request_iterator = request_iterator

    async def _forward_requests(self):
        async for request in self._request_iterator:
            self.request_cnt += 1
            yield request

    def __aiter__(self):
        return self._forward_requests()


class CountingResponseIterator:
    def __init__(self, response_iterator):
        self.response_cnt = 0
        self._response_iterator = response_iterator

    async def _forward_responses(self):
        async for response in self._response_iterator:
            self.response_cnt += 1
            yield response

    def __aiter__(self):
        return self._forward_responses()


class AdhocGenericHandler(grpc.GenericRpcHandler):
    """A generic handler to plugin testing server methods on the fly."""

    _handler: grpc.RpcMethodHandler

    def __init__(self):
        self._handler = None

    def set_adhoc_handler(self, handler: grpc.RpcMethodHandler):
        self._handler = handler

    def service(self, handler_call_details):
        if handler_call_details.method == ADHOC_METHOD:
            return self._handler
        else:
            return None
