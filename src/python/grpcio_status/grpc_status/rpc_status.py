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

import collections
import sys

import google.protobuf.message
from google.rpc import status_pb2
import grpc

from ._common import GRPC_DETAILS_METADATA_KEY
from ._common import StatusDetailsMetadataDecodeError
from ._common import StatusDetailsMetadataValueError
from ._common import code_to_grpc_status_code


class _Status(
    collections.namedtuple("_Status", ("code", "details", "trailing_metadata")),
    grpc.Status,
):
    pass


def from_call(call):
    """Returns a google.rpc.status.Status message corresponding to a given grpc.Call.

    This is an EXPERIMENTAL API.

    Args:
      call: A grpc.Call instance.

    Returns:
      A google.rpc.status.Status message representing the status of the RPC, or
      None if no status details metadata is found in the call.

    Raises:
      StatusDetailsMetadataDecodeError: If the binary metadata in
        'grpc-status-details-bin' cannot be decoded into a Status proto.
      StatusDetailsMetadataValueError: If the gRPC call's code or details are
        inconsistent with the status code and message inside of the
        google.rpc.status.Status.

    Note:
      Both StatusDetailsMetadataDecodeError and StatusDetailsMetadataValueError
      inherit from ValueError (and StatusDetailsMetadataDecodeError also
      inherits from google.protobuf.message.DecodeError). Therefore, callers
      can catch ValueError as a catch-all for any status details error.

    Examples:
      Catching specific error types:
      ```python
      try:
          status = rpc_status.from_call(call)
      except rpc_status.StatusDetailsMetadataDecodeError:
          # Handle malformed or corrupted metadata
          ...
      except rpc_status.StatusDetailsMetadataValueError:
          # Handle inconsistent status code or message
          ...
      ```

      Catch-all solution catching any status details error:
      ```python
      try:
          status = rpc_status.from_call(call)
      except ValueError:
          # Catches both StatusDetailsMetadataDecodeError and
          # StatusDetailsMetadataValueError
          ...
      ```

      Catching DecodeError directly:
      ```python
      try:
          status = rpc_status.from_call(call)
      except google.protobuf.message.DecodeError:
          # StatusDetailsMetadataDecodeError also inherits from DecodeError
          ...
      ```
    """
    if call.trailing_metadata() is None:
        return None
    for key, value in call.trailing_metadata():
        if key == GRPC_DETAILS_METADATA_KEY:
            try:
                rich_status = status_pb2.Status.FromString(value)
            except google.protobuf.message.DecodeError as decode_err:
                raise StatusDetailsMetadataDecodeError(
                    decode_err
                ) from decode_err
            if call.code().value[0] != rich_status.code:
                raise StatusDetailsMetadataValueError(
                    "Code in Status proto (%s) doesn't match status code (%s)"
                    % (code_to_grpc_status_code(rich_status.code), call.code())
                )
            if call.details() != rich_status.message:
                raise StatusDetailsMetadataValueError(
                    "Message in Status proto (%s) doesn't match status details"
                    " (%s)" % (rich_status.message, call.details())
                )
            return rich_status
    return None


def to_status(status):
    """Convert a google.rpc.status.Status message to grpc.Status.

    This is an EXPERIMENTAL API.

    Args:
      status: a google.rpc.status.Status message representing the non-OK status
        to terminate the RPC with and communicate it to the client.

    Returns:
      A grpc.Status instance representing the input google.rpc.status.Status message.
    """
    return _Status(
        code=code_to_grpc_status_code(status.code),
        details=status.message,
        trailing_metadata=(
            (GRPC_DETAILS_METADATA_KEY, status.SerializeToString()),
        ),
    )


__all__ = [
    "from_call",
    "to_status",
]

if sys.version_info[0] >= 3 and sys.version_info[1] >= 6:
    from . import _async as aio  # pylint: disable=unused-import

    __all__ += ["aio"]
