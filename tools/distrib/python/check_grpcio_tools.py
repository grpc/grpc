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

import make_grpcio_tools as _make

OUT_OF_DATE_MESSAGE = """file {} is out of date

Have you called tools/distrib/python/make_grpcio_tools.py since upgrading protobuf?"""

submodule_commit_hash = _make.protobuf_submodule_commit_hash()

with open(_make.GRPC_PYTHON_PROTOC_LIB_DEPS, 'r') as _protoc_lib_deps_file:
    content = _protoc_lib_deps_file.read().splitlines()

testString = (
    _make.COMMIT_HASH_PREFIX + submodule_commit_hash + _make.COMMIT_HASH_SUFFIX)

if testString not in content:
    print(OUT_OF_DATE_MESSAGE.format(_make.GRPC_PYTHON_PROTOC_LIB_DEPS))
    raise SystemExit(1)
