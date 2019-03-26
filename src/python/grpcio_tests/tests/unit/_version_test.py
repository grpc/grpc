# Copyright 2018 gRPC authors.
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
"""Test for grpc.__version__"""

import unittest
import grpc
import logging
from grpc import _grpcio_metadata


class VersionTest(unittest.TestCase):

    def test_get_version(self):
        self.assertEqual(grpc.__version__, _grpcio_metadata.__version__)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
