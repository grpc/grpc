#!/usr/bin/env bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Run tensorflow's rpcbench_test.
set -ex

# Enter the gRPC repo root
cd $(dirname $0)/../..

GRPC_GIT_REPO=$(git remote get-url origin)
GRPC_GIT_COMMIT=$(git show --format="%H" --no-patch)

# Expect tensorflow checked out in a directory next to grpc
cd ../tensorflow

# Patch tensorflow to use a specific version of gRPC
sed -i -e "s|set(GRPC_URL \(.*\))|set(GRPC_URL ${GRPC_GIT_REPO})|g" tensorflow/contrib/cmake/external/grpc.cmake
sed -i -e "s|set(GRPC_TAG \(.*\))|set(GRPC_TAG ${GRPC_GIT_COMMIT})|g" tensorflow/contrib/cmake/external/grpc.cmake
# Check that grpc.cmake was patched successfully
grep -q "${GRPC_GIT_REPO}" tensorflow/contrib/cmake/external/grpc.cmake
grep -q "${GRPC_GIT_COMMIT}" tensorflow/contrib/cmake/external/grpc.cmake

# Show the patch
git diff

yes "" | ./configure
bazel run -c opt //tensorflow/core/distributed_runtime:rpcbench_test -- --benchmarks=all

# TODO(jtattermusch): collect data as JSON and use tools/gcp/utils/big_query_utils.py to upload.
