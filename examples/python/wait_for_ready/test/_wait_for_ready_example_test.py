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
"""Tests of the wait-for-ready example."""

import asyncio
import unittest
import logging

from examples.python.wait_for_ready import wait_for_ready_example
from examples.python.wait_for_ready import asyncio_wait_for_ready_example


class WaitForReadyExampleTest(unittest.TestCase):

    def test_wait_for_ready_example(self):
        wait_for_ready_example.main()
        # No unhandled exception raised, no deadlock, test passed!

    def test_asyncio_wait_for_ready_example(self):
        asyncio.get_event_loop().run_until_complete(
            asyncio_wait_for_ready_example.main())
        # No unhandled exception raised, no deadlock, test passed!


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
