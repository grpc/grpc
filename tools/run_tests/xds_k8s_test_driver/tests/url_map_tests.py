# Copyright 2021 The gRPC Authors
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

import importlib
import os

from absl import logging
from absl.testing import absltest
from framework import xds_url_map_testcase  # Needed for xDS flags

_TEST_CASE_FOLDER = os.path.join(os.path.dirname(__file__), 'url_map')


def load_tests(loader: absltest.TestLoader, unused_tests, unused_pattern):
    return loader.discover(_TEST_CASE_FOLDER, pattern='*_test.py')


if __name__ == '__main__':
    absltest.main(failfast=True)
