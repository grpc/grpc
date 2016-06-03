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

"""Shared implementation."""

import logging

import six

import grpc
from grpc._cython import cygrpc

_EMPTY_METADATA = cygrpc.Metadata(())

CYGRPC_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY = {
    cygrpc.ConnectivityState.idle: grpc.ChannelConnectivity.IDLE,
    cygrpc.ConnectivityState.connecting: grpc.ChannelConnectivity.CONNECTING,
    cygrpc.ConnectivityState.ready: grpc.ChannelConnectivity.READY,
    cygrpc.ConnectivityState.transient_failure:
        grpc.ChannelConnectivity.TRANSIENT_FAILURE,
    cygrpc.ConnectivityState.fatal_failure:
        grpc.ChannelConnectivity.FATAL_FAILURE,
}

CYGRPC_STATUS_CODE_TO_STATUS_CODE = {
    cygrpc.StatusCode.ok: grpc.StatusCode.OK,
    cygrpc.StatusCode.cancelled: grpc.StatusCode.CANCELLED,
    cygrpc.StatusCode.unknown: grpc.StatusCode.UNKNOWN,
    cygrpc.StatusCode.invalid_argument: grpc.StatusCode.INVALID_ARGUMENT,
    cygrpc.StatusCode.deadline_exceeded: grpc.StatusCode.DEADLINE_EXCEEDED,
    cygrpc.StatusCode.not_found: grpc.StatusCode.NOT_FOUND,
    cygrpc.StatusCode.already_exists: grpc.StatusCode.ALREADY_EXISTS,
    cygrpc.StatusCode.permission_denied: grpc.StatusCode.PERMISSION_DENIED,
    cygrpc.StatusCode.unauthenticated: grpc.StatusCode.UNAUTHENTICATED,
    cygrpc.StatusCode.resource_exhausted: grpc.StatusCode.RESOURCE_EXHAUSTED,
    cygrpc.StatusCode.failed_precondition: grpc.StatusCode.FAILED_PRECONDITION,
    cygrpc.StatusCode.aborted: grpc.StatusCode.ABORTED,
    cygrpc.StatusCode.out_of_range: grpc.StatusCode.OUT_OF_RANGE,
    cygrpc.StatusCode.unimplemented: grpc.StatusCode.UNIMPLEMENTED,
    cygrpc.StatusCode.internal: grpc.StatusCode.INTERNAL,
    cygrpc.StatusCode.unavailable: grpc.StatusCode.UNAVAILABLE,
    cygrpc.StatusCode.data_loss: grpc.StatusCode.DATA_LOSS,
}
STATUS_CODE_TO_CYGRPC_STATUS_CODE = {
    grpc_code: cygrpc_code
    for cygrpc_code, grpc_code in six.iteritems(
        CYGRPC_STATUS_CODE_TO_STATUS_CODE)
}


def metadata(application_metadata):
  return _EMPTY_METADATA if application_metadata is None else cygrpc.Metadata(
      cygrpc.Metadatum(key, value) for key, value in application_metadata)


def _transform(message, transformer, exception_message):
  if transformer is None:
    return message
  else:
    try:
      return transformer(message)
    except Exception:  # pylint: disable=broad-except
      logging.exception(exception_message)
      return None


def serialize(message, serializer):
  return _transform(message, serializer, 'Exception serializing message!')


def deserialize(serialized_message, deserializer):
  return _transform(serialized_message, deserializer,
                    'Exception deserializing message!')
