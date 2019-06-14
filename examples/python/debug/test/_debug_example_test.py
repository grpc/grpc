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

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import logging
import unittest

from examples.python.debug import debug_server
from examples.python.debug import send_message
from examples.python.debug import get_stats

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)

_FAILURE_RATE = 0.5
_NUMBER_OF_MESSAGES = 100

_ADDR_TEMPLATE = 'localhost:%d'


class DebugExampleTest(unittest.TestCase):

    def test_channelz_example(self):
        server = debug_server.create_server(
            addr='[::]:0', failure_rate=_FAILURE_RATE)
        port = server.add_insecure_port('[::]:0')
        server.start()
        address = _ADDR_TEMPLATE % port

        send_message.run(addr=address, n=_NUMBER_OF_MESSAGES)
        get_stats.run(addr=address)
        server.stop(None)
        # No unhandled exception raised, test passed!


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
