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

from __future__ import print_function
import os
import sys
import glob
import yaml

os.chdir(os.path.dirname(sys.argv[0]) + '/../..')

out = {}

out['libs'] = [{
    'name':
        're2',
    'build' :
        'private',
    'language':
        'c++',
    'secure':
        False,
    'src':
        sorted(glob.glob('third_party/re2/re2/*.cc') +
               glob.glob('third_party/re2/util/*.cc')),
    'headers':
        sorted(
            glob.glob('third_party/re2/re2/*.h') +
            glob.glob('third_party/re2/util/*.h')),
}]

print(yaml.dump(out))
