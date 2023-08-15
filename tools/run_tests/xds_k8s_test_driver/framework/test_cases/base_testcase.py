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

from typing import Optional
import unittest

from absl import logging
from absl.testing import absltest


class BaseTestCase(absltest.TestCase):
    def run(self, result: Optional[unittest.TestResult] = None) -> None:
        super().run(result)
        test_errors = [error for test, error in result.errors if test is self]
        test_failures = [
            failure for test, failure in result.failures if test is self
        ]
        test_unexpected_successes = [
            test for test in result.unexpectedSuccesses if test is self
        ]
        test_skipped = next(
            (reason for test, reason in result.skipped if test is self),
            None,
        )
        # Assume one test case will only have one status.
        if test_errors or test_failures:
            logging.info("----- TestCase %s FAILED -----", self.id())
            if test_errors:
                self._print_error_list(test_errors, is_unexpected_error=True)
            if test_failures:
                self._print_error_list(test_failures)
        elif test_unexpected_successes:
            logging.info(
                "----- TestCase %s UNEXPECTEDLY SUCCEEDED -----", self.id()
            )
        elif test_skipped:
            logging.info("----- TestCase %s SKIPPED -----", self.id())
            logging.info("Reason for skipping: %s", test_skipped)
        else:
            logging.info("----- TestCase %s PASSED -----", self.id())

    def _print_error_list(
        self, errors: list[str], is_unexpected_error: bool = False
    ) -> None:
        # FAILUREs are those errors explicitly signalled using
        # the TestCase.assert*() methods.
        for err in errors:
            logging.error(
                "%s Traceback in %s:\n%s",
                "ERROR" if is_unexpected_error else "FAILURE",
                self.id(),
                err,
            )
