# Copyright 2024 gRPC authors.
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
"""Test of gRPC Python's utilities."""

import logging
import unittest

from grpc._utilities import first_version_is_lower


class UtilityTest(unittest.TestCase):
    def testVersionCheck(self):
        self.assertTrue(first_version_is_lower("1.2.3", "1.2.4"))
        self.assertTrue(first_version_is_lower("1.2.4", "10.2.3"))
        self.assertTrue(first_version_is_lower("1.2.3", "1.2.3.dev0"))
        self.assertFalse(first_version_is_lower("NOT_A_VERSION", "1.2.4"))
        self.assertFalse(first_version_is_lower("1.2.3", "NOT_A_VERSION"))
        self.assertFalse(first_version_is_lower("1.2.4", "1.2.3"))
        self.assertFalse(first_version_is_lower("10.2.3", "1.2.4"))
        self.assertFalse(first_version_is_lower("1.2.3dev0", "1.2.3"))
        self.assertFalse(first_version_is_lower("1.2.3", "1.2.3dev0"))
        self.assertFalse(first_version_is_lower("1.2.3.dev0", "1.2.3"))


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
