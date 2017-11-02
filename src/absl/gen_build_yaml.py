#!/usr/bin/env python2.7

# Copyright 2017 gRPC authors.
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

import glob
import os
import sys
import yaml

os.chdir(os.path.dirname(sys.argv[0])+'/../..')

out = {}

out['libs'] = [{
    'name': 'absl',
    'build': 'private',
    'language': 'c++',
    'secure': 'no',
    'defaults': 'absl',
    'src': [],
    'headers': sorted(
        glob.glob('third_party/abseil-cpp/absl/*/*.h') +
        glob.glob('third_party/abseil-cpp/absl/*/*/*.h')),
}]

print yaml.dump(out)
