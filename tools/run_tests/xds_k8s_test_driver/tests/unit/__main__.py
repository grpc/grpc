# Copyright 2023 The gRPC Authors
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
"""Discover and run all unit tests recursively."""

import pathlib

from absl.testing import absltest


def load_tests(loader: absltest.TestLoader, unused_tests, unused_pattern):
    unit_tests_root = pathlib.Path(__file__).parent
    return loader.discover(f"{unit_tests_root}", pattern="*_test.py")


if __name__ == "__main__":
    absltest.main()
