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

import os
import importlib

from framework import xds_url_map_testcase

from absl import logging
from absl.testing import absltest

_TEST_CASE_FOLDER = os.path.join(os.path.dirname(__file__), 'url_map')


def load_tests(loader: absltest.TestLoader, unused_tests, unused_pattern):
    test_modules = []
    for file_name in os.listdir(_TEST_CASE_FOLDER):
        if not file_name.endswith('_test.py'):
            continue
        module_name = 'tests.url_map.' + file_name[:-3]
        test_modules.append(importlib.import_module(module_name, package=None))
        logging.info('Loading test module: %s', module_name)

    return xds_url_map_testcase.load_tests(loader, *test_modules)


if __name__ == '__main__':
    absltest.main(failfast=True)
