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
"""Poll statistics from the server."""

import asyncio
import logging
import argparse
import grpc

from grpc_channelz.v1 import channelz_pb2
from grpc_channelz.v1 import channelz_pb2_grpc


async def run(addr: str) -> None:
    async with grpc.aio.insecure_channel(addr) as channel:
        channelz_stub = channelz_pb2_grpc.ChannelzStub(channel)
        response = await channelz_stub.GetServers(
            channelz_pb2.GetServersRequest(start_server_id=0))
        print('Info for all servers: %s' % response)


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--addr',
                        nargs=1,
                        type=str,
                        default='[::]:50051',
                        help='the address to request')
    args = parser.parse_args()
    run(addr=args.addr)


if __name__ == '__main__':
    logging.basicConfig()
    asyncio.get_event_loop().run_until_complete(main())
