# Copyright 2026 gRPC authors.
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
"""Tests that messages received in several transport slices are reassembled."""

import array
from concurrent import futures
import logging
import unittest

import grpc

_SERVICE_NAME = "test"
_UNARY_UNARY = "UnaryUnary"
_STREAM_STREAM = "StreamStream"

# Sizes around the boundaries that matter when a received message is copied
# out of its slices: empty, a single slice, more than one HTTP/2 data frame,
# and either side of 1 MiB, above which the copy runs without the GIL.
_MESSAGE_SIZES = (0, 1, 2**14 + 1, 2**20 - 1, 2**20, 3 * 2**20)


def _payload(size):
    """Returns `size` bytes whose content depends on their position.

    A dropped, duplicated, or reordered slice therefore changes the message,
    and the pattern compresses well enough for the gzip variant of the test to
    really send compressed messages.
    """
    return array.array("I", range(size // 4 + 1)).tobytes()[:size]


def _handle_unary_unary(request, servicer_context):
    return request


def _handle_stream_stream(request_iterator, servicer_context):
    for request in request_iterator:
        yield request


_METHOD_HANDLERS = {
    _UNARY_UNARY: grpc.unary_unary_rpc_method_handler(_handle_unary_unary),
    _STREAM_STREAM: grpc.stream_stream_rpc_method_handler(
        _handle_stream_stream
    ),
}


class MessageReassemblyTest(unittest.TestCase):
    _COMPRESSION = None

    def setUp(self):
        self._server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=10),
            options=(("grpc.so_reuseport", 0),),
            compression=self._COMPRESSION,
        )
        self._server.add_registered_method_handlers(
            _SERVICE_NAME, _METHOD_HANDLERS
        )
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._channel = grpc.insecure_channel(
            "localhost:%d" % port, compression=self._COMPRESSION
        )

    def tearDown(self):
        self._server.stop(0)
        self._channel.close()

    def testUnaryUnary(self):
        multi_callable = self._channel.unary_unary(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_UNARY),
            _registered_method=True,
        )
        for size in _MESSAGE_SIZES:
            request = _payload(size)
            self.assertEqual(request, multi_callable(request), size)

    def testStreamStream(self):
        requests = [_payload(size) for size in _MESSAGE_SIZES]
        response_iterator = self._channel.stream_stream(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_STREAM),
            _registered_method=True,
        )(iter(requests))
        responses = list(response_iterator)
        self.assertEqual(len(requests), len(responses))
        for request, response in zip(requests, responses):
            self.assertEqual(request, response, len(request))


class CompressedMessageReassemblyTest(MessageReassemblyTest):
    _COMPRESSION = grpc.Compression.Gzip


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
