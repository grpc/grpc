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

out = {}

try:
    out['libs'] = [{
        'name': 'upb',
        'build': 'all',
        'language': 'c',
        'src': [
            "third_party/upb/upb/decode.c",
            "third_party/upb/upb/encode.c",
            "third_party/upb/upb/msg.c",
            "third_party/upb/upb/port.c",
            "third_party/upb/upb/table.c",
            "third_party/upb/upb/upb.c",
            "third_party/upb/upb/def.c",
            "third_party/upb/upb/reflection.c",
            "third_party/upb/upb/text_encode.c",
            "src/core/ext/upb-generated/google/protobuf/any.upb.c",
            "src/core/ext/upb-generated/google/protobuf/descriptor.upb.c",
            "src/core/ext/upb-generated/google/protobuf/duration.upb.c",
            "src/core/ext/upb-generated/google/protobuf/empty.upb.c",
            "src/core/ext/upb-generated/google/protobuf/struct.upb.c",
            "src/core/ext/upb-generated/google/protobuf/timestamp.upb.c",
            "src/core/ext/upb-generated/google/protobuf/wrappers.upb.c",
            "src/core/ext/upbdefs-generated/google/protobuf/any.upbdefs.c",
            "src/core/ext/upbdefs-generated/google/protobuf/descriptor.upbdefs.c",
            "src/core/ext/upbdefs-generated/google/protobuf/duration.upbdefs.c",
            "src/core/ext/upbdefs-generated/google/protobuf/empty.upbdefs.c",
            "src/core/ext/upbdefs-generated/google/protobuf/struct.upbdefs.c",
            "src/core/ext/upbdefs-generated/google/protobuf/timestamp.upbdefs.c",
            "src/core/ext/upbdefs-generated/google/protobuf/wrappers.upbdefs.c",
        ],
        'headers': [
            "third_party/upb/upb/decode.h",
            "third_party/upb/upb/encode.h",
            "third_party/upb/upb/msg.h",
            "third_party/upb/upb/port_def.inc",
            "third_party/upb/upb/port_undef.inc",
            "third_party/upb/upb/table.int.h",
            "third_party/upb/upb/upb.h",
            "third_party/upb/upb/upb.hpp",
            "third_party/upb/upb/def.h",
            "third_party/upb/upb/def.hpp",
            "third_party/upb/upb/reflection.h",
            "third_party/upb/upb/text_encode.h",
            "src/core/ext/upb-generated/google/protobuf/any.upb.h",
            "src/core/ext/upb-generated/google/protobuf/descriptor.upb.h",
            "src/core/ext/upb-generated/google/protobuf/duration.upb.h",
            "src/core/ext/upb-generated/google/protobuf/empty.upb.h",
            "src/core/ext/upb-generated/google/protobuf/struct.upb.h",
            "src/core/ext/upb-generated/google/protobuf/timestamp.upb.h",
            "src/core/ext/upb-generated/google/protobuf/wrappers.upb.h",
            "src/core/ext/upbdefs-generated/google/protobuf/any.upbdefs.h",
            "src/core/ext/upbdefs-generated/google/protobuf/descriptor.upbdefs.h",
            "src/core/ext/upbdefs-generated/google/protobuf/duration.upbdefs.h",
            "src/core/ext/upbdefs-generated/google/protobuf/empty.upbdefs.h",
            "src/core/ext/upbdefs-generated/google/protobuf/struct.upbdefs.h",
            "src/core/ext/upbdefs-generated/google/protobuf/timestamp.upbdefs.h",
            "src/core/ext/upbdefs-generated/google/protobuf/wrappers.upbdefs.h",
        ],
        'secure': False,
    }]
except:
    pass

print(yaml.dump(out))
