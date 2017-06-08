#!/bin/bash

# Copyright 2016 gRPC authors.
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

cd $(dirname $0)/../../../

# First check if bazel is installed on the machine. If it is, then we don't need
# to invoke the docker bazel.
if [ -x "$(command -v bazel)" ]
then
  cd third_party/protobuf
  bazel query 'deps('$1')'
else
  docker build -t bazel_local_img tools/dockerfile/test/sanity
  docker run -v "$(realpath .):/src/grpc/:ro" \
    -w /src/grpc/third_party/protobuf         \
    bazel_local_img                           \
    bazel query 'deps('$1')'
fi
