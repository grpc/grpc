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
        'all',
    'language':
        'c',
    'secure':
        False,
    'src':
        sorted(glob.glob('src/libuv/src/**/*.c', recursive=True)),
    'headers':
        sorted(
            glob.glob('src/libuv/src/**/*.[c,h]', recursive=True) +
            glob.glob('src/libuv/include/**/*.h', recursive=True) +
            glob.glob('third_party/libuv/src/**/*.[c,h]', recursive=True) +
            glob.glob('third_party/libuv/include/**/*.[c,h]', recursive=True)),
}]

print(yaml.dump(out))
