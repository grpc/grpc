# Copyright 2019 The gRPC authors
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
"""gRPC's local credential API."""

import enum
import grpc
from grpc._cython import cygrpc


@enum.unique
class LocalConnectType(enum.Enum):
    """Type of local connections for which local channel/server credentials will be applied.

    Attributes:
      UDS: Unix domain socket connections
      LOCAL_TCP: Local TCP connections.
    """
    UDS = cygrpc.LocalConnectType.uds
    LOCAL_TCP = cygrpc.LocalConnectType.local_tcp


def local_channel_credentials(local_connect_type=LocalConnectType.LOCAL_TCP):
    """Creates a local ChannelCredentials used for local connections.

    Args:
      local_connect_type: Local connection type (either UDS or LOCAL_TCP)

    Returns:
      A ChannelCredentials for use with a local Channel
    """
    return grpc.ChannelCredentials(
        cygrpc.channel_credentials_local(local_connect_type.value))


def local_server_credentials(local_connect_type=LocalConnectType.LOCAL_TCP):
    """Creates a local ServerCredentials used for local connections.

    Args:
      local_connect_type: Local connection type (either UDS or LOCAL_TCP)

    Returns:
      A ServerCredentials for use with a local Server
    """
    return grpc.ServerCredentials(
        cygrpc.server_credentials_local(local_connect_type.value))
