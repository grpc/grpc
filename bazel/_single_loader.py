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

import importlib.util
import pkgutil
from typing import Optional, Sequence
import unittest


class SingleLoader:

    def __init__(
        self,
        target_module: str,
        unittest_path: Optional[str] = None,
        test_patterns: Optional[Sequence[str]] = None,
    ):
        loader = unittest.TestLoader()
        loader.testNamePatterns = test_patterns
        self.suite = unittest.TestSuite()
        tests = []

        search_paths = [unittest_path] if unittest_path else ["."]
        for importer, module_name, is_package in pkgutil.walk_packages(
            search_paths
        ):
            if (
                module_name == target_module
                or module_name.endswith("." + target_module)
            ):
                try:
                    spec = importer.find_spec(module_name)
                    if spec is None:
                        spec = importlib.util.find_spec(module_name)
                    if spec is not None:
                        module = importlib.util.module_from_spec(spec)
                        spec.loader.exec_module(module)
                        tests.append(loader.loadTestsFromModule(module))
                except Exception as e:
                    raise AssertionError(
                        f"Error loading module {module_name}: {e}"
                    )

        if len(tests) != 1:
            raise AssertionError(f"Expected only 1 test module. Found {tests}")

        self.suite.addTest(tests[0])

    def loadTestsFromNames(
        self, names: Sequence[str], module: Optional[str] = None
    ) -> unittest.TestSuite:
        return self.suite
