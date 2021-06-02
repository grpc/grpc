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

import os
import sys
import glob
import yaml

_SOURCE_FILES_TO_IGNORE = {
    'third_party/s2a_core/src/token_manager/fake_access_token_manager.cc',
}

os.chdir(os.path.dirname(sys.argv[0]) + '/../..')

out = {}

out['libs'] = [{
    'name':
        's2a_core',
    'build':
        'private',
    'language':
        'c',
    'secure':
        False,
    'src':
        sorted([
            src_file
            for src_file in glob.glob('third_party/s2a_core/src/**/*.cc')
            if ((not os.path.basename(src_file).endswith('_test.cc')) and
                (src_file not in _SOURCE_FILES_TO_IGNORE))
        ] + glob.glob('third_party/s2a_core/src/proto/upb-generated/proto/*.c')
              ),
    'headers':
        sorted(
            glob.glob('third_party/s2a_core/include/*.h') +
            glob.glob('third_party/s2a_core/src/**/*.h') +
            glob.glob('third_party/s2a_core/src/proto/upb-generated/proto/*.h')
        ),
}]

print(yaml.dump(out))
