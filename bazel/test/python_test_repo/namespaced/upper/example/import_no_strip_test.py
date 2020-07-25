# Copyright 2020 the gRPC authors.
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


class ImportTest(unittest.TestCase):
    def test_import(self):
        from foo.bar.namespaced.upper.example.namespaced_example_pb2 import NamespacedExample
        namespaced_example = NamespacedExample()
        namespaced_example.value = "hello"
        # Dummy assert, important part is namespaced example was imported.
        self.assertEqual(namespaced_example.value, "hello")

    def test_grpc(self):
        from foo.bar.namespaced.upper.example.namespaced_example_pb2_grpc import NamespacedServiceStub
        # No error from import
        self.assertEqual(1, 1)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main()
