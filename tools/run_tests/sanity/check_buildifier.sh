#! /bin/bash -x
# Copyright 2019 The gRPC Authors
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
#
# The script to sanitize Bazel files

GIT_ROOT="$(dirname "$0")/../../.."
TMP_ROOT="/tmp/buildifier_grpc"
rm -rf "${TMP_ROOT}"
git clone -- "$GIT_ROOT" "$TMP_ROOT"
buildifier -r -v -mode=diff $TMP_ROOT
result=$?

if [[ ${result} != 0 ]]; then
    echo "==========BUILDIFIER CHECK FAILED=========="
    echo "Please try using the following script to fix automatically:"
    echo ""
    echo "    tools/distrib/buildifier_format_code.sh"
    echo ""
else
    echo "==========BUILDIFIER CHECK PASSED=========="
fi
