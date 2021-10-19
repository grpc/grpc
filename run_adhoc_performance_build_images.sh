#!/usr/bin/env bash
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

# Enter the gRPC repo root.
cd "$(dirname "$0")"

# Set up environment variables.
LOAD_TEST_PREFIX="$USER"

PREBUILT_IMAGE_PREFIX="gcr.io/grpc-testing/e2etesting/pre_built_workers/${LOAD_TEST_PREFIX}"
UNIQUE_IDENTIFIER="$(date +%Y%m%d%H%M%S)"
ROOT_DIRECTORY_OF_DOCKERFILES="../test-infra/containers/pre_built_workers/"
# Head of the workspace checked out by Kokoro.
GRPC_GITREF="$(git show --format="%H" --no-patch)"
GRPC_CORE_GITREF=${GRPC_GITREF}

# # Build and push prebuilt images for running tests.
time ../test-infra/bin/prepare_prebuilt_workers \
     -l "cxx:${GRPC_CORE_GITREF}" \
     -p "${PREBUILT_IMAGE_PREFIX}" \
     -t "${UNIQUE_IDENTIFIER}" \
     -r "${ROOT_DIRECTORY_OF_DOCKERFILES}"
