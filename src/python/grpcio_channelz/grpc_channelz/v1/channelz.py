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

import threading
import grpc
from grpc._cython import cygrpc

import grpc_channelz.v1.channelz_pb2 as _channelz_pb2
import grpc_channelz.v1.channelz_pb2_grpc as _channelz_pb2_grpc

from google.protobuf import json_format

from . import _channelz_page


class ChannelzServicer(_channelz_pb2_grpc.ChannelzServicer):
    """Servicer handling RPCs for service statuses."""

    @staticmethod
    def GetTopChannels(request, context):
        try:
            return json_format.Parse(
                cygrpc.channelz_get_top_channels(request.start_channel_id),
                _channelz_pb2.GetTopChannelsResponse(),
            )
        except (ValueError, json_format.ParseError) as e:
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))

    @staticmethod
    def GetServers(request, context):
        try:
            return json_format.Parse(
                cygrpc.channelz_get_servers(request.start_server_id),
                _channelz_pb2.GetServersResponse(),
            )
        except (ValueError, json_format.ParseError) as e:
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))

    @staticmethod
    def GetServer(request, context):
        try:
            return json_format.Parse(
                cygrpc.channelz_get_server(request.server_id),
                _channelz_pb2.GetServerResponse(),
            )
        except ValueError as e:
            context.set_code(grpc.StatusCode.NOT_FOUND)
            context.set_details(str(e))
        except json_format.ParseError as e:
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))

    @staticmethod
    def GetServerSockets(request, context):
        try:
            return json_format.Parse(
                cygrpc.channelz_get_server_sockets(request.server_id,
                                                   request.start_socket_id,
                                                   request.max_results),
                _channelz_pb2.GetServerSocketsResponse(),
            )
        except ValueError as e:
            context.set_code(grpc.StatusCode.NOT_FOUND)
            context.set_details(str(e))
        except json_format.ParseError as e:
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))

    @staticmethod
    def GetChannel(request, context):
        try:
            return json_format.Parse(
                cygrpc.channelz_get_channel(request.channel_id),
                _channelz_pb2.GetChannelResponse(),
            )
        except ValueError as e:
            context.set_code(grpc.StatusCode.NOT_FOUND)
            context.set_details(str(e))
        except json_format.ParseError as e:
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))

    @staticmethod
    def GetSubchannel(request, context):
        try:
            return json_format.Parse(
                cygrpc.channelz_get_subchannel(request.subchannel_id),
                _channelz_pb2.GetSubchannelResponse(),
            )
        except ValueError as e:
            context.set_code(grpc.StatusCode.NOT_FOUND)
            context.set_details(str(e))
        except json_format.ParseError as e:
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))

    @staticmethod
    def GetSocket(request, context):
        try:
            return json_format.Parse(
                cygrpc.channelz_get_socket(request.socket_id),
                _channelz_pb2.GetSocketResponse(),
            )
        except ValueError as e:
            context.set_code(grpc.StatusCode.NOT_FOUND)
            context.set_details(str(e))
        except json_format.ParseError as e:
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))


def add_channelz_servicer(server):
    """Add Channelz servicer to a server. Channelz servicer is in charge of
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
      server: grpc.Server to which Channelz service will be added.
    """
    _channelz_pb2_grpc.add_ChannelzServicer_to_server(ChannelzServicer(),
                                                      server)


def serve_channelz_page(host=None, port=None):
    """Start an HTTP server serving Channelz Pages.

    The pages will be served under: http://<URL>/gdebug/channelz/
    The gRPC server/client have to be started prior to requesting the server.
    Otherwise, the gRPC C-Core won't be started, and it will throw SIGABORT
    terminating the process.

    To close the HTTP server cleanly, you need to call 'shutdown()' and
    'server_close()' on the returned HTTP server instance. For more details,
    please check the definition of Python's BaseServer:
    https://docs.python.org/3/library/socketserver.html#socketserver.BaseServer

    This is an EXPERIMENTAL API.

    Args:
      host: a str indicates the address of the HTTP server.
      port: a int indicates the tcp port of the HTTP server.

    Returns:
      An http.Server.HTTPServer instance that is already started serving.
    """
    if host is None or port is None:
        raise ValueError('"host" and "port" can\'t be None')
    http_server = _channelz_page._create_http_server((host, port))

    serving_thread = threading.Thread(target=http_server.serve_forever)
    serving_thread.daemon = True
    serving_thread.start()
    return http_server
