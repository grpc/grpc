#!/usr/bin/env python

# Copyright 2018 gRPC authors.
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

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

# set of files that are allowed to use the raw GRPC_ARG_* types
_EXCEPTIONS = set([
    'src/core/lib/channel/channel_args.cc',
    'src/core/lib/channel/channel_args.h',
])

_BANNED = set([
    "GRPC_ARG_POINTER",
])

errors = 0
num_files = 0
for root, dirs, files in os.walk('src/core'):
    for filename in files:
        num_files += 1
        path = os.path.join(root, filename)
        if path in _EXCEPTIONS: continue
        with open(path) as f:
            text = f.read()
        for banned in _BANNED:
            if banned in text:
                print('Illegal use of "%s" in %s' % (banned, path))
                errors += 1

assert errors == 0
# This check comes about from this issue:
# https://github.com/grpc/grpc/issues/15381
# Basically, a change rendered this script useless and we did not realize it.
# This dumb check ensures that this type of issue doesn't occur again.
assert num_files > 300  # we definitely have more than 300 files
