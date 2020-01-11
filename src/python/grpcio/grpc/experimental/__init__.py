# Copyright 2018 gRPC authors.
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
"""gRPC's experimental APIs.

These APIs are subject to be removed during any minor version release.
"""


class ChannelOptions(object):
    """Indicates a channel option unique to gRPC Python.

     This enumeration is part of an EXPERIMENTAL API.

     Attributes:
       SingleThreadedUnaryStream: Perform unary-stream RPCs on a single thread.
    """
    SingleThreadedUnaryStream = "SingleThreadedUnaryStream"


class UsageError(Exception):
    """Raised by the gRPC library to indicate usage not allowed by the API."""
