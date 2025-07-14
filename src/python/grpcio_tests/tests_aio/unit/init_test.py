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
import os
import unittest

# Install typeguard BEFORE any imports
if os.environ.get("TYPEGUARD_ENABLED") == "1":
    print("Installing typeguard import hooks...")
    from typeguard import install_import_hook
    install_import_hook('grpc.aio')
    install_import_hook('grpc.aio._channel')
    print("Typeguard import hooks installed successfully")


class TestInit(unittest.TestCase):
    def test_grpc(self):
        import grpc  # pylint: disable=wrong-import-position

        channel = grpc.aio.insecure_channel("phony")
        self.assertIsInstance(channel, grpc.aio.Channel)

    def test_grpc_dot_aio(self):
        import grpc.aio  # pylint: disable=wrong-import-position

        channel = grpc.aio.insecure_channel("phony")
        self.assertIsInstance(channel, grpc.aio.Channel)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
