#!/bin/sh
# Copyright 2017 gRPC authors.
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


set -e

# Make sure that there is no path from a known unsecure target
# to an SSL library

[[ $(bazel query "somepath(//test/cpp/microbenchmarks:helpers, //external:libssl)" 2>/dev/null | wc -l) -eq 0 ]]

# Fall through with the exit code of that command
