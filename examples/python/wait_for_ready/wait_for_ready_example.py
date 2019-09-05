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
"""The Python example of utilizing wait-for-ready flag."""

from __future__ import print_function
import logging
from concurrent import futures
from contextlib import contextmanager
import socket
import threading

import grpc

from examples import helloworld_pb2
from examples import helloworld_pb2_grpc

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)


@contextmanager
def get_free_loopback_tcp_port():
    if socket.has_ipv6:
        tcp_socket = socket.socket(socket.AF_INET6)
    else:
        tcp_socket = socket.socket(socket.AF_INET)
    tcp_socket.bind(('', 0))
    address_tuple = tcp_socket.getsockname()
    yield "localhost:%s" % (address_tuple[1])
    tcp_socket.close()


class Greeter(helloworld_pb2_grpc.GreeterServicer):

    def SayHello(self, request, unused_context):
        return helloworld_pb2.HelloReply(message='Hello, %s!' % request.name)


def create_server(server_address):
    server = grpc.server(futures.ThreadPoolExecutor())
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    bound_port = server.add_insecure_port(server_address)
    assert bound_port == int(server_address.split(':')[-1])
    return server


def process(stub, wait_for_ready=None):
    try:
        response = stub.SayHello(
            helloworld_pb2.HelloRequest(name='you'),
            wait_for_ready=wait_for_ready)
        message = response.message
    except grpc.RpcError as rpc_error:
        assert rpc_error.code() == grpc.StatusCode.UNAVAILABLE
        assert not wait_for_ready
        message = rpc_error
    else:
        assert wait_for_ready
    _LOGGER.info("Wait-for-ready %s, client received: %s", "enabled"
                 if wait_for_ready else "disabled", message)


def main():
    # Pick a random free port
    with get_free_loopback_tcp_port() as server_address:

        # Register connectivity event to notify main thread
        transient_failure_event = threading.Event()

        def wait_for_transient_failure(channel_connectivity):
            if channel_connectivity == grpc.ChannelConnectivity.TRANSIENT_FAILURE:
                transient_failure_event.set()

        # Create gRPC channel
        channel = grpc.insecure_channel(server_address)
        channel.subscribe(wait_for_transient_failure)
        stub = helloworld_pb2_grpc.GreeterStub(channel)

        # Fire an RPC without wait_for_ready
        thread_disabled_wait_for_ready = threading.Thread(
            target=process, args=(stub, False))
        thread_disabled_wait_for_ready.start()
        # Fire an RPC with wait_for_ready
        thread_enabled_wait_for_ready = threading.Thread(
            target=process, args=(stub, True))
        thread_enabled_wait_for_ready.start()

    # Wait for the channel entering TRANSIENT FAILURE state.
    transient_failure_event.wait()
    server = create_server(server_address)
    server.start()

    # Expected to fail with StatusCode.UNAVAILABLE.
    thread_disabled_wait_for_ready.join()
    # Expected to success.
    thread_enabled_wait_for_ready.join()

    server.stop(None)
    channel.close()


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    main()
