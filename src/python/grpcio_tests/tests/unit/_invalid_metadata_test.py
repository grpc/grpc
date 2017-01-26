# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Test of RPCs made against gRPC Python's application-layer API."""

import unittest

import grpc

from tests.unit.framework.common import test_constants

_SERIALIZE_REQUEST = lambda bytestring: bytestring * 2
_DESERIALIZE_REQUEST = lambda bytestring: bytestring[len(bytestring) // 2:]
_SERIALIZE_RESPONSE = lambda bytestring: bytestring * 3
_DESERIALIZE_RESPONSE = lambda bytestring: bytestring[:len(bytestring) // 3]

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'


def _unary_unary_multi_callable(channel):
    return channel.unary_unary(_UNARY_UNARY)


def _unary_stream_multi_callable(channel):
    return channel.unary_stream(
        _UNARY_STREAM,
        request_serializer=_SERIALIZE_REQUEST,
        response_deserializer=_DESERIALIZE_RESPONSE)


def _stream_unary_multi_callable(channel):
    return channel.stream_unary(
        _STREAM_UNARY,
        request_serializer=_SERIALIZE_REQUEST,
        response_deserializer=_DESERIALIZE_RESPONSE)


def _stream_stream_multi_callable(channel):
    return channel.stream_stream(_STREAM_STREAM)


class InvalidMetadataTest(unittest.TestCase):

    def setUp(self):
        self._channel = grpc.insecure_channel('localhost:8080')
        self._unary_unary = _unary_unary_multi_callable(self._channel)
        self._unary_stream = _unary_stream_multi_callable(self._channel)
        self._stream_unary = _stream_unary_multi_callable(self._channel)
        self._stream_stream = _stream_stream_multi_callable(self._channel)

    def testUnaryRequestBlockingUnaryResponse(self):
        request = b'\x07\x08'
        metadata = (('InVaLiD', 'UnaryRequestBlockingUnaryResponse'),)
        expected_error_details = "metadata was invalid: %s" % metadata
        with self.assertRaises(ValueError) as exception_context:
            self._unary_unary(request, metadata=metadata)
        self.assertIn(expected_error_details, str(exception_context.exception))

    def testUnaryRequestBlockingUnaryResponseWithCall(self):
        request = b'\x07\x08'
        metadata = (('InVaLiD', 'UnaryRequestBlockingUnaryResponseWithCall'),)
        expected_error_details = "metadata was invalid: %s" % metadata
        with self.assertRaises(ValueError) as exception_context:
            self._unary_unary.with_call(request, metadata=metadata)
        self.assertIn(expected_error_details, str(exception_context.exception))

    def testUnaryRequestFutureUnaryResponse(self):
        request = b'\x07\x08'
        metadata = (('InVaLiD', 'UnaryRequestFutureUnaryResponse'),)
        expected_error_details = "metadata was invalid: %s" % metadata
        response_future = self._unary_unary.future(request, metadata=metadata)
        with self.assertRaises(grpc.RpcError) as exception_context:
            response_future.result()
        self.assertEqual(exception_context.exception.details(),
                         expected_error_details)
        self.assertEqual(exception_context.exception.code(),
                         grpc.StatusCode.INTERNAL)
        self.assertEqual(response_future.details(), expected_error_details)
        self.assertEqual(response_future.code(), grpc.StatusCode.INTERNAL)

    def testUnaryRequestStreamResponse(self):
        request = b'\x37\x58'
        metadata = (('InVaLiD', 'UnaryRequestStreamResponse'),)
        expected_error_details = "metadata was invalid: %s" % metadata
        response_iterator = self._unary_stream(request, metadata=metadata)
        with self.assertRaises(grpc.RpcError) as exception_context:
            next(response_iterator)
        self.assertEqual(exception_context.exception.details(),
                         expected_error_details)
        self.assertEqual(exception_context.exception.code(),
                         grpc.StatusCode.INTERNAL)
        self.assertEqual(response_iterator.details(), expected_error_details)
        self.assertEqual(response_iterator.code(), grpc.StatusCode.INTERNAL)

    def testStreamRequestBlockingUnaryResponse(self):
        request_iterator = (b'\x07\x08'
                            for _ in range(test_constants.STREAM_LENGTH))
        metadata = (('InVaLiD', 'StreamRequestBlockingUnaryResponse'),)
        expected_error_details = "metadata was invalid: %s" % metadata
        with self.assertRaises(ValueError) as exception_context:
            self._stream_unary(request_iterator, metadata=metadata)
        self.assertIn(expected_error_details, str(exception_context.exception))

    def testStreamRequestBlockingUnaryResponseWithCall(self):
        request_iterator = (b'\x07\x08'
                            for _ in range(test_constants.STREAM_LENGTH))
        metadata = (('InVaLiD', 'StreamRequestBlockingUnaryResponseWithCall'),)
        expected_error_details = "metadata was invalid: %s" % metadata
        multi_callable = _stream_unary_multi_callable(self._channel)
        with self.assertRaises(ValueError) as exception_context:
            multi_callable.with_call(request_iterator, metadata=metadata)
        self.assertIn(expected_error_details, str(exception_context.exception))

    def testStreamRequestFutureUnaryResponse(self):
        request_iterator = (b'\x07\x08'
                            for _ in range(test_constants.STREAM_LENGTH))
        metadata = (('InVaLiD', 'StreamRequestFutureUnaryResponse'),)
        expected_error_details = "metadata was invalid: %s" % metadata
        response_future = self._stream_unary.future(
            request_iterator, metadata=metadata)
        with self.assertRaises(grpc.RpcError) as exception_context:
            response_future.result()
        self.assertEqual(exception_context.exception.details(),
                         expected_error_details)
        self.assertEqual(exception_context.exception.code(),
                         grpc.StatusCode.INTERNAL)
        self.assertEqual(response_future.details(), expected_error_details)
        self.assertEqual(response_future.code(), grpc.StatusCode.INTERNAL)

    def testStreamRequestStreamResponse(self):
        request_iterator = (b'\x07\x08'
                            for _ in range(test_constants.STREAM_LENGTH))
        metadata = (('InVaLiD', 'StreamRequestStreamResponse'),)
        expected_error_details = "metadata was invalid: %s" % metadata
        response_iterator = self._stream_stream(
            request_iterator, metadata=metadata)
        with self.assertRaises(grpc.RpcError) as exception_context:
            next(response_iterator)
        self.assertEqual(exception_context.exception.details(),
                         expected_error_details)
        self.assertEqual(exception_context.exception.code(),
                         grpc.StatusCode.INTERNAL)
        self.assertEqual(response_iterator.details(), expected_error_details)
        self.assertEqual(response_iterator.code(), grpc.StatusCode.INTERNAL)


if __name__ == '__main__':
    unittest.main(verbosity=2)
