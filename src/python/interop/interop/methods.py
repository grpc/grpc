# Copyright 2015, Google Inc.
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

"""Implementations of interoperability test methods."""

from grpc_early_adopter import utilities

from interop import empty_pb2
from interop import messages_pb2

def _empty_call(request):
  return empty_pb2.Empty()

EMPTY_CALL = utilities.unary_unary_rpc_method(
    _empty_call, empty_pb2.Empty.SerializeToString, empty_pb2.Empty.FromString,
    empty_pb2.Empty.SerializeToString, empty_pb2.Empty.FromString)


def _unary_call(request):
  return messages_pb2.SimpleResponse(
      payload=messages_pb2.Payload(
          type=messages_pb2.COMPRESSABLE,
          body=b'\x00' * request.response_size))

UNARY_CALL = utilities.unary_unary_rpc_method(
    _unary_call, messages_pb2.SimpleRequest.SerializeToString,
    messages_pb2.SimpleRequest.FromString,
    messages_pb2.SimpleResponse.SerializeToString,
    messages_pb2.SimpleResponse.FromString)


def _streaming_output_call(request):
  for response_parameters in request.response_parameters:
    yield messages_pb2.StreamingOutputCallResponse(
        payload=messages_pb2.Payload(
            type=request.response_type,
            body=b'\x00' * response_parameters.size))

STREAMING_OUTPUT_CALL = utilities.unary_stream_rpc_method(
    _streaming_output_call,
    messages_pb2.StreamingOutputCallRequest.SerializeToString,
    messages_pb2.StreamingOutputCallRequest.FromString,
    messages_pb2.StreamingOutputCallResponse.SerializeToString,
    messages_pb2.StreamingOutputCallResponse.FromString)


def _streaming_input_call(request_iterator):
  aggregate_size = 0
  for request in request_iterator:
    if request.payload and request.payload.body:
      aggregate_size += len(request.payload.body)
  return messages_pb2.StreamingInputCallResponse(
      aggregated_payload_size=aggregate_size)

STREAMING_INPUT_CALL = utilities.stream_unary_rpc_method(
    _streaming_input_call,
    messages_pb2.StreamingInputCallRequest.SerializeToString,
    messages_pb2.StreamingInputCallRequest.FromString,
    messages_pb2.StreamingInputCallResponse.SerializeToString,
    messages_pb2.StreamingInputCallResponse.FromString)


def _full_duplex_call(request_iterator):
  for request in request_iterator:
    yield messages_pb2.StreamingOutputCallResponse(
        payload=messages_pb2.Payload(
            type=request.payload.type,
            body=b'\x00' * request.response_parameters[0].size))

FULL_DUPLEX_CALL = utilities.stream_stream_rpc_method(
    _full_duplex_call,
    messages_pb2.StreamingOutputCallRequest.SerializeToString,
    messages_pb2.StreamingOutputCallRequest.FromString,
    messages_pb2.StreamingOutputCallResponse.SerializeToString,
    messages_pb2.StreamingOutputCallResponse.FromString)

# NOTE(nathaniel): Apparently this is the same as the full-duplex call?
HALF_DUPLEX_CALL = utilities.stream_stream_rpc_method(
    _full_duplex_call,
    messages_pb2.StreamingOutputCallRequest.SerializeToString,
    messages_pb2.StreamingOutputCallRequest.FromString,
    messages_pb2.StreamingOutputCallResponse.SerializeToString,
    messages_pb2.StreamingOutputCallResponse.FromString)
