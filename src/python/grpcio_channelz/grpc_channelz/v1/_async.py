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
"""AsyncIO version of Channelz servicer."""

from grpc.experimental import aio
from grpc_channelz.v1._servicer import ChannelzServicer as _SyncChannelzServicer
import grpc_channelz.v1.channelz_pb2 as _channelz_pb2
import grpc_channelz.v1.channelz_pb2_grpc as _channelz_pb2_grpc


class ChannelzServicer(_channelz_pb2_grpc.ChannelzServicer):
    """AsyncIO servicer for handling RPCs for service statuses."""

    @staticmethod
    async def GetTopChannels(
        request: _channelz_pb2.GetTopChannelsRequest,
        context: aio.ServicerContext,
    ) -> _channelz_pb2.GetTopChannelsResponse:
        return _SyncChannelzServicer.GetTopChannels(request, context)

    @staticmethod
    async def GetServers(
        request: _channelz_pb2.GetServersRequest, context: aio.ServicerContext
    ) -> _channelz_pb2.GetServersResponse:
        return _SyncChannelzServicer.GetServers(request, context)

    @staticmethod
    async def GetServer(
        request: _channelz_pb2.GetServerRequest, context: aio.ServicerContext
    ) -> _channelz_pb2.GetServerResponse:
        return _SyncChannelzServicer.GetServer(request, context)

    @staticmethod
    async def GetServerSockets(
        request: _channelz_pb2.GetServerSocketsRequest,
        context: aio.ServicerContext,
    ) -> _channelz_pb2.GetServerSocketsResponse:
        return _SyncChannelzServicer.GetServerSockets(request, context)

    @staticmethod
    async def GetChannel(
        request: _channelz_pb2.GetChannelRequest, context: aio.ServicerContext
    ) -> _channelz_pb2.GetChannelResponse:
        return _SyncChannelzServicer.GetChannel(request, context)

    @staticmethod
    async def GetSubchannel(
        request: _channelz_pb2.GetSubchannelRequest,
        context: aio.ServicerContext,
    ) -> _channelz_pb2.GetSubchannelResponse:
        return _SyncChannelzServicer.GetSubchannel(request, context)

    @staticmethod
    async def GetSocket(
        request: _channelz_pb2.GetSocketRequest, context: aio.ServicerContext
    ) -> _channelz_pb2.GetSocketResponse:
        return _SyncChannelzServicer.GetSocket(request, context)
