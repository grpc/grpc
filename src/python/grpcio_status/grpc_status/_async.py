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
"""Reference implementation for status mapping in gRPC Python."""

import google.protobuf.message
from google.rpc import status_pb2
from grpc.experimental import aio

from ._common import GRPC_DETAILS_METADATA_KEY
from ._common import StatusDetailsMetadataDecodeError
from ._common import StatusDetailsMetadataValueError
from ._common import code_to_grpc_status_code


async def from_call(call: aio.Call):
    """Returns a google.rpc.status.Status message from a given grpc.aio.Call.

    This is an EXPERIMENTAL API.

    Args:
      call: An grpc.aio.Call instance.

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
          status = await rpc_status.aio.from_call(call)
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
          status = await rpc_status.aio.from_call(call)
      except ValueError:
          # Catches both StatusDetailsMetadataDecodeError and
          # StatusDetailsMetadataValueError
          ...
      ```

      Catching DecodeError directly:
      ```python
      try:
          status = await rpc_status.aio.from_call(call)
      except google.protobuf.message.DecodeError:
          # StatusDetailsMetadataDecodeError also inherits from DecodeError
          ...
      ```
    """
    code = await call.code()
    details = await call.details()
    trailing_metadata = await call.trailing_metadata()
    if trailing_metadata is None:
        return None
    for key, value in trailing_metadata:
        if key == GRPC_DETAILS_METADATA_KEY:
            try:
                rich_status = status_pb2.Status.FromString(value)
            except google.protobuf.message.DecodeError as decode_err:
                raise StatusDetailsMetadataDecodeError(decode_err) from decode_err
            if code.value[0] != rich_status.code:
                raise StatusDetailsMetadataValueError(
                    "Code in Status proto (%s) doesn't match status code (%s)"
                    % (code_to_grpc_status_code(rich_status.code), code)
                )
            if details != rich_status.message:
                raise StatusDetailsMetadataValueError(
                    "Message in Status proto (%s) doesn't match status details"
                    " (%s)" % (rich_status.message, details)
                )
            return rich_status
    return None


__all__ = [
    "from_call",
]
