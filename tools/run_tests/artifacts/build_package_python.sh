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

set -ex

cd "$(dirname "$0")/../../.."

mkdir -p artifacts/

# All the python packages have been built in the artifact phase already
# and we only collect them here to deliver them to the distribtest phase.
# Jenkins flow (deprecated)
cp -r "${EXTERNAL_GIT_ROOT}"/platform={windows,linux,macos}/artifacts/python_*/* artifacts/ || true
# Kokoro flow
cp -r "${EXTERNAL_GIT_ROOT}"/input_artifacts/python_*/* artifacts/ || true

# TODO: all the artifact builder configurations generate a grpcio-VERSION.tar.gz
# source distribution package, and only one of them will end up
# in the artifacts/ directory. They should be all equivalent though.
