# Copyright 2019 The gRPC Authors
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

import collections
import pkgutil
import traceback

from google.protobuf import json_format
from grpc._cython import cygrpc
from six.moves.BaseHTTPServer import BaseHTTPRequestHandler, HTTPServer  # pylint: disable=wrong-import-order
from six.moves.urllib.parse import parse_qs, urlparse  # pylint: disable=wrong-import-order

from grpc_channelz.v1 import _renderer as __renderer
from grpc_channelz.v1 import channelz_pb2 as _channelz_pb2

_TEMPLATE_FOLDER = './templates'
_URI_PREFIX = '/gdebug/channelz/'

_PAGE = collections.namedtuple('_PAGE', ['title', 'template', 'handler'])
_SERVER_AND_SOCKETS = collections.namedtuple('_SERVER_AND_SOCKETS',
                                             ['server', 'listen_sockets'])

_renderer = __renderer.Renderer()


def _fetch_template(template_name):
    return pkgutil.get_data(__name__, '%s/%s' % (_TEMPLATE_FOLDER,
                                                 template_name)).decode('ASCII')


_base_template = _fetch_template('base.html')


def _parse_path_and_args_from_url(path):
    parsed = urlparse(path)
    args = parse_qs(parsed.query)
    for key in args:
        if isinstance(args[key], list) and len(args[key]) == 1:
            args[key] = args[key][0]
    return parsed.path, args


class _NotFound(Exception):
    pass


class _BadRequest(Exception):
    pass


def _homepage_handler(base_page, unused_args):
    return _renderer.format(base_page)


def _topchannels_handler(base_page, args):
    start_channel_id = int(args.get('start_channel_id', '0'))
    topchannels = json_format.Parse(
        cygrpc.channelz_get_top_channels(start_channel_id),
        _channelz_pb2.GetTopChannelsResponse(),
    ).channel
    if not topchannels:
        raise _NotFound(
            'No channel found for "start_channel_id"==%d' % start_channel_id)
    return _renderer.format(
        base_page,
        topchannels=topchannels,
        num_channel=len(topchannels),
        min_id=min(channel.ref.channel_id for channel in topchannels),
        max_id=max(channel.ref.channel_id for channel in topchannels))


def _channel_handler(base_page, args):
    if 'channel_id' not in args:
        raise _BadRequest('"channel_id" cannot be empty')
    channel_id = int(args.get('channel_id'))
    channel = json_format.Parse(
        cygrpc.channelz_get_channel(channel_id),
        _channelz_pb2.GetChannelResponse(),
    ).channel

    nested_channels = []
    for ref in channel.channel_ref:
        nested_channels.append(
            json_format.Parse(
                cygrpc.channelz_get_channel(ref.channel_id),
                _channelz_pb2.GetChannelResponse(),
            ).channel)

    subchannels = []
    for ref in channel.subchannel_ref:
        subchannels.append(
            json_format.Parse(
                cygrpc.channelz_get_subchannel(ref.subchannel_id),
                _channelz_pb2.GetSubchannelResponse(),
            ).subchannel)

    return _renderer.format(
        base_page,
        channel=channel,
        nested_channels=nested_channels,
        subchannels=subchannels)


def _subchannel_handler(base_page, args):
    if 'subchannel_id' not in args:
        raise _BadRequest('"subchannel_id" cannot be empty')
    subchannel_id = int(args.get('subchannel_id'))
    subchannel = json_format.Parse(
        cygrpc.channelz_get_subchannel(subchannel_id),
        _channelz_pb2.GetSubchannelResponse(),
    ).subchannel

    sockets = []
    for ref in subchannel.socket_ref:
        sockets.append(
            json_format.Parse(
                cygrpc.channelz_get_socket(ref.socket_id),
                _channelz_pb2.GetSocketResponse(),
            ).socket)

    return _renderer.format(base_page, subchannel=subchannel, sockets=sockets)


def _socket_handler(base_page, args):
    if 'socket_id' not in args:
        raise _BadRequest('"socket_id" cannot be empty')
    socket_id = int(args.get('socket_id'))
    socket = json_format.Parse(
        cygrpc.channelz_get_socket(socket_id),
        _channelz_pb2.GetSocketResponse(),
    ).socket

    return _renderer.format(base_page, socket=socket)


def _servers_handler(base_page, args):
    start_server_id = int(args.get('start_server_id', '0'))
    servers = json_format.Parse(
        cygrpc.channelz_get_servers(start_server_id),
        _channelz_pb2.GetServersResponse(),
    ).server

    if not servers:
        raise _NotFound(
            'No server found for "start_server_id"==%d' % start_server_id)

    servers_n_sockets = []
    for server in servers:
        listen_sockets = []
        for ref in server.listen_socket:
            listen_sockets.append(
                json_format.Parse(
                    cygrpc.channelz_get_socket(ref.socket_id),
                    _channelz_pb2.GetSocketResponse(),
                ).socket)
        servers_n_sockets.append(_SERVER_AND_SOCKETS(server, listen_sockets))

    return _renderer.format(
        base_page,
        num_servers=len(servers),
        min_id=min(server.ref.server_id for server in servers),
        max_id=max(server.ref.server_id for server in servers),
        servers_n_sockets=servers_n_sockets)


def _serversockets_handler(base_page, args):
    if 'server_id' not in args:
        raise _BadRequest('"server_id" cannot be empty')
    server_id = int(args.get('server_id'))
    start_socket_id = int(args.get('start_socket_id', '0'))
    server = json_format.Parse(
        cygrpc.channelz_get_server(server_id),
        _channelz_pb2.GetServerResponse(),
    ).server
    serversocket_refs = json_format.Parse(
        cygrpc.channelz_get_server_sockets(server_id, start_socket_id, 0),
        _channelz_pb2.GetServerSocketsResponse(),
    ).socket_ref

    if not serversocket_refs:
        raise _NotFound(
            'No server socket found for "server_id"==%d' % server_id)

    serversockets = []
    for ref in serversocket_refs:
        serversockets.append(
            json_format.Parse(
                cygrpc.channelz_get_socket(ref.socket_id),
                _channelz_pb2.GetSocketResponse(),
            ).socket)

    return _renderer.format(
        base_page,
        server=server,
        serversockets=serversockets,
        num_serversockets=len(serversockets),
        min_id=min(ref.socket_id for ref in serversocket_refs),
        max_id=max(ref.socket_id for ref in serversocket_refs))


_SERVING_PAGES = {
    '':
    _PAGE('<nil>', 'index.html', _homepage_handler),
    'channel':
    _PAGE('Channel', 'channel.html', _channel_handler),
    'servers':
    _PAGE('Servers', 'servers.html', _servers_handler),
    'serversockets':
    _PAGE('ServerSockets', 'serversockets.html', _serversockets_handler),
    'socket':
    _PAGE('Socket', 'socket.html', _socket_handler),
    'subchannel':
    _PAGE('Subchannel', 'subchannel.html', _subchannel_handler),
    'topchannels':
    _PAGE('TopChannels', 'topchannels.html', _topchannels_handler),
}


class _RequestHandler(BaseHTTPRequestHandler):

    def _set_ok_headers(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/html')
        self.end_headers()

    def _handle(self):
        if not self.path.startswith(_URI_PREFIX):
            raise _NotFound()

        path, args = _parse_path_and_args_from_url(self.path)
        request_page = path[len(_URI_PREFIX):]
        if request_page not in _SERVING_PAGES:
            raise _NotFound('Page not found')
        serving_page = _SERVING_PAGES[request_page]

        base_page = _renderer.format(
            _base_template,
            title=serving_page.title,
            content=_fetch_template(serving_page.template))

        full_page = serving_page.handler(base_page, args)
        self._set_ok_headers()
        self.wfile.write(full_page.encode('ASCII'))

    def do_GET(self):
        try:
            self._handle()
        except _BadRequest as e:
            self.send_error(400, str(e))
        except _NotFound as e:
            self.send_error(404, str(e))
        except ValueError as e:
            stack_str = traceback.format_exc()
            if '_cython.cygrpc.channelz' in stack_str:
                # Return 404 if the Channelz fetched nothing from C-Core
                self.send_error(404, str(e))
            else:
                # Otherwise, return 400
                self.send_error(400, str(e))
        except RuntimeError as e:
            stack_str = traceback.format_exc()
            if str(e) == 'The gRPC library is not initialized.':
                self.send_error(503)  # 503 - Service Unavailable
        except Exception as e:  # pylint: disable=broad-except
            self.send_error(500)


def _create_http_server(addr):
    return HTTPServer(addr, _RequestHandler)
