# Copyright 2019 The gRPC Authors.
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
import logging
import unittest

from grpc.experimental import aio
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase


class TestInsecureChannel(AioTestBase):

    async def test_insecure_channel(self):
        server_target, _ = await start_test_server()  # pylint: disable=unused-variable

        channel = aio.insecure_channel(server_target)
        self.assertIsInstance(channel, aio.Channel)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
