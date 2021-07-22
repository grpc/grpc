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

_HEADER_FILES_TO_IGNORE = {
    'third_party/s2a_core/s2a/src/crypto/s2a_aead_crypter_test_util.h',
    'third_party/s2a_core/s2a/src/handshaker/s2a_proxy_test_util.h',
    'third_party/s2a_core/s2a/src/test_util/s2a_test_data.h',
    'third_party/s2a_core/s2a/src/test_util/s2a_test_util.h',
    'third_party/s2a_core/s2a/src/token_manager/fake_access_token_manager.h',
}

_SOURCE_FILES_TO_IGNORE = {
    'third_party/s2a_core/s2a/src/crypto/s2a_aead_crypter_test_util.cc',
    'third_party/s2a_core/s2a/src/handshaker/s2a_proxy_test_util.cc',
    'third_party/s2a_core/s2a/src/token_manager/fake_access_token_manager.cc',
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
            for src_file in glob.glob('third_party/s2a_core/s2a/src/**/*.cc')
            if ((not os.path.basename(src_file).endswith('_test.cc')) and
                (src_file not in _SOURCE_FILES_TO_IGNORE))
        ] + glob.glob('third_party/s2a_core/s2a/src/proto/upb-generated/proto/*.c')),
    'headers':
        sorted([
            header_file
            for header_file in glob.glob('third_party/s2a_core/s2a/src/**/*.h')
            if (header_file not in _HEADER_FILES_TO_IGNORE)
        ] + glob.glob('third_party/s2a_core/s2a/include/*.h') +
            glob.glob('third_party/s2a_core/s2a/src/proto/upb-generated/proto/*.h')
        ),
}]

print(yaml.dump(out))
