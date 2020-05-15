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

import grpc

from grpc.experimental import aio
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase

from tests.unit import resources

_PRIVATE_KEY = resources.private_key()
_CERTIFICATE_CHAIN = resources.certificate_chain()
_TEST_ROOT_CERTIFICATES = resources.test_root_certificates()


class TestChannel(AioTestBase):

    async def test_insecure_channel(self):
        server_target, _ = await start_test_server()  # pylint: disable=unused-variable

        channel = aio.insecure_channel(server_target)
        self.assertIsInstance(channel, aio.Channel)

    async def test_secure_channel(self):
        server_target, _ = await start_test_server(secure=True)  # pylint: disable=unused-variable
        credentials = grpc.ssl_channel_credentials(
            root_certificates=_TEST_ROOT_CERTIFICATES,
            private_key=_PRIVATE_KEY,
            certificate_chain=_CERTIFICATE_CHAIN,
        )
        secure_channel = aio.secure_channel(server_target, credentials)

        self.assertIsInstance(secure_channel, aio.Channel)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
