#!/bin/bash
# Copyright 2025 gRPC authors.
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


set -ex

# PHASE 0: query bazel for information we'll need
cd $(dirname $0)/../..
tools/bazel query --noimplicit_deps --output=xml 'deps(test/...)' > tools/artifact_gen/test_deps.xml

# PHASE 1: generate artifacts
cd tools/artifact_gen
bazel run :artifact_gen -- \
	`pwd`/test_deps.xml

