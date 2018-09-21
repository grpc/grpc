# Copyright 2016 gRPC authors.
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
"""Reference implementation for channelz in gRPC Python."""

import grpc
from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool

from grpc_channelz import channelz_pb2 as _channelz_pb2
from grpc_channelz import channelz_pb2_grpc as _channelz_pb2_grpc

class ChannelzService(_channelz_pb2_grpc.Channelz):
    """Servicer handling RPCs for Channelz."""
    pass


def enable_channelz_service(server):
    """Enables server reflection on a server.
    """
    _channelz_pb2_grpc.add_Channelz_to_server(
        ChannelzService(), server)
