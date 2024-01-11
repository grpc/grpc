#!/bin/bash
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

# change to root directory
cd $(dirname $0)/../..
REPO_ROOT=$(pwd)

# grep targets with manual tag, which is not included in a result of bazel build using ...
# let's get a list of them using query command and pass it to gen_compilation_database.py
export MANUAL_TARGETS=$(bazel query 'attr("tags", "manual", tests(//test/cpp/...))' | grep -v _on_ios)

# generate a clang compilation database for all C/C++ sources in the repo.
tools/distrib/gen_compilation_database.py \
  --include_headers \
  --ignore_system_headers \
  --dedup_targets \
  "//:*" \
  "//src/core/..." \
  "//src/compiler/..." \
  "//test/core/..." \
  "//test/cpp/..." \
  $MANUAL_TARGETS
