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

import argparse
import asyncio
import logging

from grpc.experimental import aio

from src.proto.grpc.testing import worker_service_pb2_grpc
from tests_aio.benchmark import worker_servicer


async def run_worker_server(port: int) -> None:
    server = aio.server()

    servicer = worker_servicer.WorkerServicer()
    worker_service_pb2_grpc.add_WorkerServiceServicer_to_server(
        servicer, server)

    server.add_insecure_port('[::]:{}'.format(port))

    await server.start()

    await servicer.wait_for_quit()
    await server.stop(None)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    parser = argparse.ArgumentParser(
        description='gRPC Python performance testing worker')
    parser.add_argument('--driver_port',
                        type=int,
                        dest='port',
                        help='The port the worker should listen on')
    parser.add_argument('--uvloop',
                        action='store_true',
                        help='Use uvloop or not')
    args = parser.parse_args()

    if args.uvloop:
        import uvloop
        asyncio.set_event_loop_policy(uvloop.EventLoopPolicy())
        loop = uvloop.new_event_loop()
        asyncio.set_event_loop(loop)

    asyncio.get_event_loop().run_until_complete(run_worker_server(args.port))
