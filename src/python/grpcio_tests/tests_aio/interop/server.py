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
"""The gRPC interoperability test server using AsyncIO stack."""

import argparse
import asyncio
import logging

import grpc

from tests.interop import server as interop_server_lib
from tests_aio.unit import _test_server

logging.basicConfig(level=logging.DEBUG)
_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.DEBUG)


async def serve():
    args = interop_server_lib.parse_interop_server_arguments()

    if args.use_tls or args.use_alts:
        credentials = interop_server_lib.get_server_credentials(args.use_tls)
        address, server = await _test_server.start_test_server(
            port=args.port, secure=True, server_credentials=credentials
        )
    else:
        address, server = await _test_server.start_test_server(
            port=args.port,
            secure=False,
        )

    _LOGGER.info("Server serving at %s", address)
    await server.wait_for_termination()
    _LOGGER.info("Server stopped; exiting.")


if __name__ == "__main__":
    asyncio.get_event_loop().run_until_complete(serve())
