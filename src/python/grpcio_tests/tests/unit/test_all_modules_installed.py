# Copyright 2016 gRPC authors.
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
import logging
import grpcio_channelz
import grpcio_csds
import grpcio_admin
import grpcio_health_checking
import grpcio_reflection
import grpcio_status
import grpcio_testing
import grpcio_csm_observability
import grpcio_tools

class TestAllModulesInstalled(unittest.TestCase):
    def test_import_all_modules(self):
        # This test simply imports all the modules.
        # If any module fails to import, the test will fail.
        pass



if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=3)
