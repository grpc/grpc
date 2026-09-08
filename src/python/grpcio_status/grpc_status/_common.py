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
import grpc

_CODE_TO_GRPC_CODE_MAPPING = {x.value[0]: x for x in grpc.StatusCode}

GRPC_DETAILS_METADATA_KEY = "grpc-status-details-bin"


class StatusDetailsMetadataValueError(ValueError):
    """Raised when status details metadata is invalid or inconsistent.

    This error is raised when the status code or message inside the Status proto
    does not match the gRPC call's status code or details, or when an invalid
    status code is encountered. It also acts as the base class for
    StatusDetailsMetadataDecodeError.
    """


class StatusDetailsMetadataDecodeError(
    google.protobuf.message.DecodeError, StatusDetailsMetadataValueError
):
    """Raised when binary metadata 'grpc-status-details-bin' can't be decoded.

    Inherits from both google.protobuf.message.DecodeError and
    StatusDetailsMetadataValueError (and therefore ValueError) for backwards
    compatibility.
    """


def code_to_grpc_status_code(code):
    try:
        return _CODE_TO_GRPC_CODE_MAPPING[code]
    except KeyError:
        raise ValueError("Invalid status code %s" % code)
