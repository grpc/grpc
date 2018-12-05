# Copyright 2018 The gRPC Authors
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
"""Reference implementation for status mapping in gRPC Python."""

import grpc

# NOTE(https://github.com/bazelbuild/bazel/issues/6844)
# Due to Bazel issue, the namespace packages won't resolve correctly.
# Adding this unused-import as a workaround to avoid module-not-found error
# under Bazel builds.
import google.protobuf  # pylint: disable=unused-import
from google.rpc import status_pb2

_CODE_TO_GRPC_CODE_MAPPING = dict([(x.value[0], x) for x in grpc.StatusCode])

_GRPC_DETAILS_METADATA_KEY = 'grpc-status-details-bin'


def _code_to_grpc_status_code(code):
    try:
        return _CODE_TO_GRPC_CODE_MAPPING[code]
    except KeyError:
        raise ValueError('Invalid status code %s' % code)


def from_rpc_error(rpc_error):
    """Returns a google.rpc.status.Status message corresponding to a given grpc.RpcError.

    Args:
      call: A grpc.RpcError instance raised by an RPC.

    Returns:
      A google.rpc.status.Status message representing the status of the RPC.

    Raises:
      ValueError: If the status code, status message is inconsistent with the rich status
        inside of status details.
    """
    code, message, details = rpc_error.status()
    if details is None:
        return None
    rich_status = status_pb2.Status.FromString(details)
    if code.value[0] != rich_status.code:
        raise ValueError(
            'Code in status details (%s) doesn\'t match status code (%s)' %
            (_code_to_grpc_status_code(rich_status.code), code))
    if message != rich_status.message:
        raise ValueError(
            'Message in status details (%s) doesn\'t match status message (%s)'
            % (rich_status.message, message))
    return rich_status


def convert(status):
    """Convert a google.rpc.status.Status message to a tuple of status code, status message,
    and status details.

    Args:
      status: a google.rpc.status.Status message representing the non-OK status
        to terminate the RPC with and communicate it to the client.

    Returns:
      A 3-tuple where the first entry is the status code, the second entry is the
        status message, and the third entry is the status details.
    """
    return _code_to_grpc_status_code(
        status.code), status.message, status.SerializeToString()
