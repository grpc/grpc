# Copyright 2019 the gRPC authors.
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
"""The Python implementation of the GRPC helloworld.Greeter client."""

import contextlib
import datetime
import logging
import unittest

import simple_copy_pb2


class ImportTest(unittest.TestCase):
    def test_import(self):
        s = simple_copy_pb2.Simple()
        self.assertIsNotNone(s)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main()
