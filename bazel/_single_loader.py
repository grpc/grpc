# Copyright 2026 gRPC authors.
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
"""Loads the tests of exactly one module for the Bazel Python test runners."""

import importlib
from typing import Optional, Sequence
import unittest


class SingleLoader:
    """Loads all tests from a single module, addressed by its Bazel package.

    Shared by _gevent_test_main.py, _run_time_type_check_main.py and
    _single_module_tester.py. Any problem locating or importing the module is
    an error: the loader never falls back to other modules and never returns
    an empty suite, so a misconfigured target cannot pass without running
    tests.
    """

    def __init__(
        self,
        target_module: str,
        unittest_path: str,
        test_patterns: Optional[Sequence[str]] = None,
    ):
        """Initializes the loader.

        Args:
          target_module: Name of the test module, e.g. "channel_test".
          unittest_path: Directory containing the module, relative to the
            workspace root, e.g. native.package_name().
          test_patterns: Optional unittest -k patterns to filter tests by.
        """
        package_parts = [
            part
            for part in unittest_path.replace("\\", "/").split("/")
            if part not in ("", ".")
        ]
        module_name = ".".join(package_parts + [target_module])

        # import_module() registers the module in sys.modules, binds it to its
        # parent package and cleans up on failure. Import errors propagate
        # with their original traceback.
        module = importlib.import_module(module_name)

        loader = unittest.TestLoader()
        loader.testNamePatterns = list(test_patterns) if test_patterns else None
        self.suite = loader.loadTestsFromModule(module)
        if self.suite.countTestCases() == 0:
            patterns = f" matching {test_patterns}" if test_patterns else ""
            raise AssertionError(f"No tests found in {module_name}{patterns}")
