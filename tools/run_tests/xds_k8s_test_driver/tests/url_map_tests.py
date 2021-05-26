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
from tests.url_map import header_matching_test
from tests.url_map import path_matching_test
from framework import xds_url_map_testcase

from absl.testing import absltest


# TODO(lidiz) dynamically load modules from "./url_map" directory.
def load_tests(loader: absltest.TestLoader, unused_tests, unused_pattern):
    return xds_url_map_testcase.load_tests(loader, header_matching_test,
                                           path_matching_test)


if __name__ == '__main__':
    absltest.main(verbosity=2)
