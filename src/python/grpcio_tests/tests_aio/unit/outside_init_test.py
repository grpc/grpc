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
"""Tests behavior around the metadata mechanism."""

import asyncio
import logging
import unittest
from grpc.experimental import aio
import grpc


class TestOutsideInit(unittest.TestCase):

    def test_behavior_outside_asyncio(self):
        # Ensures non-AsyncIO object can be initiated
        channel_creds = grpc.ssl_channel_credentials()

        # Ensures AsyncIO API NOT working outside of AsyncIO
        with self.assertRaises(RuntimeError):
            aio.insecure_channel('')

        with self.assertRaises(RuntimeError):
            aio.secure_channel('', channel_creds)

        with self.assertRaises(RuntimeError):
            aio.server()

        # Ensures init_grpc_aio fail outside of AsyncIO
        with self.assertRaises(RuntimeError):
            aio.init_grpc_aio()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
