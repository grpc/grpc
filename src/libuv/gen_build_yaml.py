#!/usr/bin/env python3

# Copyright 2021 gRPC authors.
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

import yaml
import glob

out = {}

out['libs'] = [{
    'name':
        'uv',
    'build':
        'private',
    'language':
        'c',
    'secure':
        False,
    'src': ['DO_NOT_SUBMIT.cc'],
    # sorted(
    #   glob.glob('src/libuv/*.cc')
    # ),
    'headers': [],
        # sorted(
        #     glob.glob('third_party/re2/re2/*.h') +
        #     glob.glob('third_party/re2/util/*.h')),
}]

print(yaml.dump(out))
