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

import sys
import unittest

try:
    from bazel._single_loader import SingleLoader
except ImportError:
    from _single_loader import SingleLoader

if __name__ == "__main__":

    if len(sys.argv) != 3:
        print(
            f"USAGE: {sys.argv[0]} TARGET_MODULE UNITTEST_PATH", file=sys.stderr
        )
        sys.exit(1)

    target_module = sys.argv[1]
    unittest_path = sys.argv[2]

    loader = SingleLoader(target_module, unittest_path)
    runner = unittest.TextTestRunner(verbosity=0)
    result = runner.run(loader.suite)

    if not result.wasSuccessful():
        sys.exit("Test failure.")
