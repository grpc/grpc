# Copyright 2023 gRPC authors.
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
"""Base test case used for xds test suites."""

from typing import Optional, Tuple
import unittest

from absl import logging
from absl.testing import absltest


class BaseTestCase(absltest.TestCase):
    def run(self, result: unittest.TestResult = None) -> None:
        super().run(result)
        current_errors = [
            error for test, error in result.errors if test is self
        ]
        current_failures = [
            failure for test, failure in result.failures if test is self
        ]
        if current_errors or current_failures:
            logging.info("----- TestCase %s FAILED -----", self.id())
            if current_errors:
                self._print_error_list("ERROR", current_errors)
            if current_failures:
                self._print_error_list("FAILURE", current_failures)
        else:
            logging.info("----- TestCase %s PASSED -----", self.id())

    def _print_error_list(
        self, flavour: str, errors: Optional[list[str]]
    ) -> None:
        if not errors:
            return
        for err in errors:
            logging.error("%s Traceback in: %s:", flavour, self.id())
            logging.error("%s", err)
