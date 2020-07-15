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
"""Channelz debug service implementation in gRPC Python."""

import sys
import grpc

import grpc_channelz.v1.channelz_pb2_grpc as _channelz_pb2_grpc
from grpc_channelz.v1._servicer import ChannelzServicer

_add_channelz_servicer_doc = """Add Channelz servicer to a server.

Channelz servicer is in charge of
pulling information from C-Core for entire process. It will allow the
server to response to Channelz queries.

The Channelz statistic is enabled by default inside C-Core. Whether the
statistic is enabled or not is isolated from adding Channelz servicer.
That means you can query Channelz info with a Channelz-disabled channel,
and you can add Channelz servicer to a Channelz-disabled server.

The Channelz statistic can be enabled or disabled by channel option
'grpc.enable_channelz'. Set to 1 to enable, set to 0 to disable.

This is an EXPERIMENTAL API.

Args:
    server: A gRPC server to which Channelz service will be added.
"""

if sys.version_info[0] >= 3 and sys.version_info[1] >= 6:
    from grpc_channelz.v1 import _async as aio

    def add_channelz_servicer(server):

        if isinstance(server, grpc.experimental.aio.Server):
            _channelz_pb2_grpc.add_ChannelzServicer_to_server(
                aio.ChannelzServicer(), server)
        else:
            _channelz_pb2_grpc.add_ChannelzServicer_to_server(
                ChannelzServicer(), server)

    add_channelz_servicer.__doc__ = _add_channelz_servicer_doc

    __all__ = [
        "aio",
        "add_channelz_servicer",
        "ChannelzServicer",
    ]

else:

    def add_channelz_servicer(server):
        _channelz_pb2_grpc.add_ChannelzServicer_to_server(
            ChannelzServicer(), server)

    add_channelz_servicer.__doc__ = _add_channelz_servicer_doc

    __all__ = [
        "add_channelz_servicer",
        "ChannelzServicer",
    ]
