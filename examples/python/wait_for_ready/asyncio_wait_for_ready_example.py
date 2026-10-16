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
"""The Python example of utilizing wait-for-ready flag."""

import asyncio
from contextlib import contextmanager
import logging
import socket
from typing import Iterable

import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)


@contextmanager
def get_free_loopback_tcp_port() -> Iterable[str]:
    if socket.has_ipv6:
        tcp_socket = socket.socket(socket.AF_INET6)
    else:
        tcp_socket = socket.socket(socket.AF_INET)
    tcp_socket.bind(("", 0))
    address_tuple = tcp_socket.getsockname()
    yield f"localhost:{address_tuple[1]}"
    tcp_socket.close()


class Greeter(helloworld_pb2_grpc.GreeterServicer):
    async def SayHello(
        self, request: helloworld_pb2.HelloRequest, unused_context
    ) -> helloworld_pb2.HelloReply:
        return helloworld_pb2.HelloReply(message=f"Hello, {request.name}!")


def create_server(server_address: str):
    server = grpc.aio.server()
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    bound_port = server.add_insecure_port(server_address)
    assert bound_port == int(server_address.split(":")[-1])
    return server


async def process(
    stub: helloworld_pb2_grpc.GreeterStub, wait_for_ready: bool = None
) -> None:
    try:
        response = await stub.SayHello(
            helloworld_pb2.HelloRequest(name="you"),
            wait_for_ready=wait_for_ready,
        )
        message = response.message
    except grpc.aio.AioRpcError as rpc_error:
        assert rpc_error.code() == grpc.StatusCode.UNAVAILABLE
        assert not wait_for_ready
        message = rpc_error
    else:
        assert wait_for_ready
    _LOGGER.info(
        "Wait-for-ready %s, client received: %s",
        "enabled" if wait_for_ready else "disabled",
        message,
    )


async def main() -> None:
    # Pick a random free port
    with get_free_loopback_tcp_port() as server_address:
        # Create gRPC channel
        channel = grpc.aio.insecure_channel(server_address)
        stub = helloworld_pb2_grpc.GreeterStub(channel)

        # Fire an RPC without wait_for_ready
        fail_fast_task = asyncio.get_event_loop().create_task(
            process(stub, wait_for_ready=False)
        )
        # Fire an RPC with wait_for_ready
        wait_for_ready_task = asyncio.get_event_loop().create_task(
            process(stub, wait_for_ready=True)
        )

    # Wait for the channel entering TRANSIENT FAILURE state.
    state = channel.get_state()
    while state != grpc.ChannelConnectivity.TRANSIENT_FAILURE:
        await channel.wait_for_state_change(state)
        state = channel.get_state()

    # Start the server to handle the RPC
    server = create_server(server_address)
    await server.start()

    # Expected to fail with StatusCode.UNAVAILABLE.
    await fail_fast_task
    # Expected to success.
    await wait_for_ready_task

    await server.stop(None)
    await channel.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.get_event_loop().run_until_complete(main())
