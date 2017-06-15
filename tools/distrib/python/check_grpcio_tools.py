#!/usr/bin/env python

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

import cStringIO

import make_grpcio_tools as make

OUT_OF_DATE_MESSAGE = """file {} is out of date

Have you called tools/distrib/python/make_grpcio_tools.py since upgrading protobuf?"""

check_protoc_lib_deps_content = make.get_deps()

with open(make.GRPC_PYTHON_PROTOC_LIB_DEPS, 'r') as protoc_lib_deps_file:
  if protoc_lib_deps_file.read() != check_protoc_lib_deps_content:
    print(OUT_OF_DATE_MESSAGE.format(make.GRPC_PYTHON_PROTOC_LIB_DEPS))
    raise SystemExit(1)
