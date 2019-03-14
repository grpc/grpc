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

# TODO: This should ideally be in upb submodule to avoid hardcoding this here.

import re
import os
import sys
import yaml

srcs = [
  "third_party/upb/google/protobuf/descriptor.upb.c",
  "third_party/upb/upb/decode.c",
  "third_party/upb/upb/def.c",
  "third_party/upb/upb/encode.c",
  "third_party/upb/upb/handlers.c",
  "third_party/upb/upb/msg.c",
  "third_party/upb/upb/msgfactory.c",
  "third_party/upb/upb/sink.c",
  "third_party/upb/upb/table.c",
  "third_party/upb/upb/upb.c",
]

hdrs = [
  "third_party/upb/google/protobuf/descriptor.upb.h",
  "third_party/upb/upb/decode.h",
  "third_party/upb/upb/def.h",
  "third_party/upb/upb/encode.h",
  "third_party/upb/upb/handlers.h",
  "third_party/upb/upb/msg.h",
  "third_party/upb/upb/msgfactory.h",
  "third_party/upb/upb/sink.h",
  "third_party/upb/upb/upb.h",
]

os.chdir(os.path.dirname(sys.argv[0])+'/../..')

out = {}

try:
  out['libs'] = [{
      'name': 'upb',
      'defaults': 'upb',
      'build': 'private',
      'language': 'c',
      'secure': 'no',
      'src': srcs,
      'headers': hdrs,
  }]
except:
  pass

print yaml.dump(out)
