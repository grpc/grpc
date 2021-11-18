#!/bin/bash
# Copyright 2021 The gRPC Authors
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

# change to grpc repo root
cd "$(dirname "$0")/../../.."

# TODO(jtattermusch): run portserver
# TODO(jtattermusch): make sure bazel cache is persisted between runs
# TODO(jtattermusch): fix ERROR: /root/.cache/bazel/_bazel_root/eab0d61a99b6696edb3d2aff87b585e8/external/com_github_cares_cares/BUILD.bazel:91:10: Copying files failed: (Exit 1): bash failed: error executing command /bin/bash -c 'cp -f "$1" "$2"' '' third_party/cares/ares_build.h bazel-out/k8-fastbuild/bin/external/com_github_cares_cares/ares_build.h

export DOCKERFILE_DIR=tools/dockerfile/test/bazel
tools/docker_runners/run_in_docker.sh bazel test //test/...
