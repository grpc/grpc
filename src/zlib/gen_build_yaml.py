#!/usr/bin/env python2.7

# Copyright 2015 gRPC authors.
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

import re
import os
import sys
import yaml

os.chdir(os.path.dirname(sys.argv[0]) + '/../..')

out = {}

try:
    with open('third_party/zlib/CMakeLists.txt') as f:
        cmake = f.read()

    def cmpath(x):
        return 'third_party/zlib/%s' % x.replace('${CMAKE_CURRENT_BINARY_DIR}/',
                                                 '')

    def cmvar(name):
        regex = r'set\(\s*'
        regex += name
        regex += r'([^)]*)\)'
        return [cmpath(x) for x in re.search(regex, cmake).group(1).split()]

    out['libs'] = [{
        'name':
            'z',
        'zlib':
            True,
        'defaults':
            'zlib',
        'build':
            'private',
        'language':
            'c',
        'secure':
            False,
        'src':
            sorted(cmvar('ZLIB_SRCS')),
        'headers':
            sorted(cmvar('ZLIB_PUBLIC_HDRS') + cmvar('ZLIB_PRIVATE_HDRS')),
    }]
except:
    pass

print(yaml.dump(out))
