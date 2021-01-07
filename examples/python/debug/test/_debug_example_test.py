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
"""Test for gRPC Python debug example."""

import asyncio
import logging
import unittest

from examples.python.debug import debug_server
from examples.python.debug import asyncio_debug_server
from examples.python.debug import send_message
from examples.python.debug import asyncio_send_message
from examples.python.debug import get_stats
from examples.python.debug import asyncio_get_stats

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)

_FAILURE_RATE = 0.5
_NUMBER_OF_MESSAGES = 100

_ADDR_TEMPLATE = 'localhost:%d'


class DebugExampleTest(unittest.TestCase):

    def test_channelz_example(self):
        server = debug_server.create_server(addr='[::]:0',
                                            failure_rate=_FAILURE_RATE)
        port = server.add_insecure_port('[::]:0')
        server.start()
        address = _ADDR_TEMPLATE % port

        send_message.run(addr=address, n=_NUMBER_OF_MESSAGES)
        get_stats.run(addr=address)
        server.stop(None)
        # No unhandled exception raised, test passed!

    def test_asyncio_channelz_example(self):

        async def body():
            server = asyncio_debug_server.create_server(
                addr='[::]:0', failure_rate=_FAILURE_RATE)
            port = server.add_insecure_port('[::]:0')
            await server.start()
            address = _ADDR_TEMPLATE % port

            await asyncio_send_message.run(addr=address, n=_NUMBER_OF_MESSAGES)
            await asyncio_get_stats.run(addr=address)
            await server.stop(None)
            # No unhandled exception raised, test passed!

        asyncio.get_event_loop().run_until_complete(body())


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
