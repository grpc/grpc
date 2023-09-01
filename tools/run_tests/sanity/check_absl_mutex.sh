#!/bin/sh
# Copyright 2021 gRPC authors.
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

cd "$(dirname "$0")/../../.."

#
# Disallow the usage of absl::Mutex.
# Refer to https://github.com/grpc/grpc/issues/23661 and b/186685878 for context
# as to why absl::Mutex is problematic on some platforms.
#

find . \( \( -name "*.cc" \) -or \( -name "*.h" \) \) \
        -a \( \( -wholename "./src/*" \) \
            -or \( -wholename "./include/*" \) \
            -or \( -wholename "./test/*" \) \) \
        -a -not -wholename "./include/grpcpp/impl/sync.h" \
        -a -not -wholename "./src/core/lib/gprpp/sync.h" \
        -a -not -wholename "./src/core/lib/gpr/sync_abseil.cc" \
        -print0 |\
    xargs -0 grep -n "absl::Mutex" | \
    diff - /dev/null
