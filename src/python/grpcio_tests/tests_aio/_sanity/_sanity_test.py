# Copyright 2019 gRPC authors.
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

import unittest

from tests._sanity import _sanity_test


class AioSanityTest(_sanity_test.SanityTest):

    TEST_PKG_MODULE_NAME = 'tests_aio'
    TEST_PKG_PATH = 'tests_aio'


if __name__ == '__main__':
    unittest.main(verbosity=2)
