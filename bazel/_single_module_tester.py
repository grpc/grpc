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

from typing import Sequence, Optional

import unittest
import sys
import pkgutil
import importlib.util

class SingleLoader(object):
    def __init__(self, pattern: str, unittest_path: str):
        loader = unittest.TestLoader()
        self.suite = unittest.TestSuite()
        tests = []

        for importer, module_name, is_package in pkgutil.walk_packages([unittest_path]):
            if pattern in module_name:
                try:
                    spec = importer.find_spec(module_name)
                    if spec is not None:
                        module = importlib.util.module_from_spec(spec)
                        spec.loader.exec_module(module)
                        tests.append(loader.loadTestsFromModule(module))
                
                except Exception as e:
                  raise AssertionError(f"Error loading module {module_name}: {e}")

        if len(tests) != 1:            
            raise AssertionError("Expected only 1 test module. Found {}".format(tests))
        self.suite.addTest(tests[0])

    def loadTestsFromNames(self, names: Sequence[str], module: Optional[str] = None) -> unittest.TestSuite:
        return self.suite

if __name__ == "__main__":

    if len(sys.argv) != 3:
        print(f"USAGE: {sys.argv[0]} TARGET_MODULE", file=sys.stderr)
        sys.exit(1)


    target_module = sys.argv[1]
    unittest_path = sys.argv[2]

    loader = SingleLoader(target_module, unittest_path)
    runner = unittest.TextTestRunner(verbosity=0)
    result = runner.run(loader.suite)
    
    if not result.wasSuccessful():
        sys.exit("Test failure.")
