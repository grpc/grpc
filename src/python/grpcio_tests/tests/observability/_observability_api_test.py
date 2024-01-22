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
"""Test of gRPC Python Observability's application-layer API."""

import logging
import unittest

from tests.observability import _from_observability_import_star


class AllTest(unittest.TestCase):
    def testAll(self):
        expected_observability_code_elements = (
            "OpenTelemetryObservability",
            "OpenTelemetryPlugin",
        )

        self.assertCountEqual(
            expected_observability_code_elements,
            _from_observability_import_star.GRPC_OBSERVABILITY_ELEMENTS,
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
