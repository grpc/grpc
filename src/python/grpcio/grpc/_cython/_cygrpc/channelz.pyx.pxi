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


def channelz_get_top_channels(start_channel_id):
    cdef char *c_returned_str = grpc_channelz_get_top_channels(
        start_channel_id,
    )
    if c_returned_str == NULL:
        raise ValueError('Failed to get top channels, please ensure your' \
                         ' start_channel_id==%s is valid' % start_channel_id)
    return c_returned_str
    
def channelz_get_servers(start_server_id):
    cdef char *c_returned_str = grpc_channelz_get_servers(start_server_id)
    if c_returned_str == NULL:
        raise ValueError('Failed to get servers, please ensure your' \
                         ' start_server_id==%s is valid' % start_server_id)
    return c_returned_str
    
def channelz_get_server(server_id):
    cdef char *c_returned_str = grpc_channelz_get_server(server_id)
    if c_returned_str == NULL:
        raise ValueError('Failed to get the server, please ensure your' \
                         ' server_id==%s is valid' % server_id)
    return c_returned_str
    
def channelz_get_server_sockets(server_id, start_socket_id, max_results):
    cdef char *c_returned_str = grpc_channelz_get_server_sockets(
        server_id,
        start_socket_id,
        max_results,
    )
    if c_returned_str == NULL:
        raise ValueError('Failed to get server sockets, please ensure your' \
                         ' server_id==%s and start_socket_id==%s and' \
                         ' max_results==%s is valid' %
                         (server_id, start_socket_id, max_results))
    return c_returned_str
    
def channelz_get_channel(channel_id):
    cdef char *c_returned_str = grpc_channelz_get_channel(channel_id)
    if c_returned_str == NULL:
        raise ValueError('Failed to get the channel, please ensure your' \
                         ' channel_id==%s is valid' % (channel_id))
    return c_returned_str
    
def channelz_get_subchannel(subchannel_id):
    cdef char *c_returned_str = grpc_channelz_get_subchannel(subchannel_id)
    if c_returned_str == NULL:
        raise ValueError('Failed to get the subchannel, please ensure your' \
                         ' subchannel_id==%s is valid' % (subchannel_id))
    return c_returned_str
    
def channelz_get_socket(socket_id):
    cdef char *c_returned_str = grpc_channelz_get_socket(socket_id)
    if c_returned_str == NULL:
        raise ValueError('Failed to get the socket, please ensure your' \
                         ' socket_id==%s is valid' % (socket_id))
    return c_returned_str
