import os
import pkgutil
import collections
import traceback
from six.moves.BaseHTTPServer import BaseHTTPRequestHandler, HTTPServer

from grpc._cython import cygrpc
import grpc_channelz.v1.channelz_pb2 as _channelz_pb2
from google.protobuf import json_format

from ._renderer import _Renderer

_TEMPLATE_FOLDER = './templates'

_PAGE = collections.namedtuple('_PAGE', ['title', 'template', 'handler'])
_SERVER_N_SOCKETS = collections.namedtuple('_SERVER_N_SOCKETS',
                                           ['server', 'listen_sockets'])

_renderer = _Renderer()


def _fetch_template(template_name):
    return pkgutil.get_data(__name__,
                            os.path.join(_TEMPLATE_FOLDER,
                                         template_name)).decode('ASCII')


_base_template = _fetch_template('base.html')


def _parse_args(path):
    if '?' not in path:
        return {}
    args = {}
    args_list = path.split('?', 1)[1].split('&')
    for arg_str in args_list:
        key, value = arg_str.split('=')
        args[key] = value
    return args


class _NotFound(Exception):
    pass


class _BadRequest(Exception):
    pass


def _homepage_handler(render, unused_args):
    return render()


def _topchannels_handler(render, args):
    start_channel_id = int(args.get('start_channel_id', '0'))
    topchannels = json_format.Parse(
        cygrpc.channelz_get_top_channels(start_channel_id),
        _channelz_pb2.GetTopChannelsResponse(),
    ).channel
    if not topchannels:
        raise _NotFound(
            'No channel found for "start_channel_id"==%d' % start_channel_id)
    return render(
        topchannels=topchannels,
        num_channel=len(topchannels),
        min_id=min(channel.ref.channel_id for channel in topchannels),
        max_id=max(channel.ref.channel_id for channel in topchannels))


def _channel_handler(render, args):
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

    return render(
        channel=channel,
        nested_channels=nested_channels,
        subchannels=subchannels)


def _subchannel_handler(render, args):
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

    return render(subchannel=subchannel, sockets=sockets)


def _socket_handler(render, args):
    if 'socket_id' not in args:
        raise _BadRequest('"socket_id" cannot be empty')
    socket_id = int(args.get('socket_id'))
    socket = json_format.Parse(
        cygrpc.channelz_get_socket(socket_id),
        _channelz_pb2.GetSocketResponse(),
    ).socket

    return render(socket=socket)


def _servers_handler(render, args):
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
        servers_n_sockets.append(_SERVER_N_SOCKETS(server, listen_sockets))

    return render(
        num_servers=len(servers),
        min_id=min(server.ref.server_id for server in servers),
        max_id=max(server.ref.server_id for server in servers),
        servers_n_sockets=servers_n_sockets)


def _serversockets_handler(render, args):
    if 'server_id' not in args:
        raise _BadRequest('"server_id" cannot be empty')
    server_id = int(args.get('server_id'))
    start_socket_id = int(args.get('start_socket_id', '0'))
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

    return render(
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
        if not self.path.startswith('/gdebug/channelz/'):
            raise _NotFound()

        request_page = self.path[17:].split('?', 1)[0]
        if request_page not in _SERVING_PAGES:
            raise _NotFound('Page not found')
        serving_page = _SERVING_PAGES[request_page]

        base_page = _renderer.format(
            _base_template,
            title=serving_page.title,
            content=_fetch_template(serving_page.template))

        # Encapsulate _renderer and base_page into an enclosure
        def render(*args, **kwargs):
            return _renderer.format(base_page, *args, **kwargs)

        full_page = serving_page.handler(render, _parse_args(self.path))
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
                traceback.print_exc()
                self.send_error(400)
        except Exception as e:  # pylint: disable=broad-except
            traceback.print_exc()
            self.send_error(500)


def _create_http_server(addr):
    return HTTPServer(addr, _RequestHandler)
