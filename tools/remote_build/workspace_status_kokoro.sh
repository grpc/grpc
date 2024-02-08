#!/bin/bash
# Copyright 2018 The gRPC Authors
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

# Adds additional labels to results page for Bazel RBE builds on Kokoro

# Provide a way to go from Bazel RBE links back to Kokoro job results
# which is important for debugging test infrastructure problems.
# TODO(jtattermusch): replace this workaround by something more user-friendly.
echo "KOKORO_RESULTSTORE_URL https://source.cloud.google.com/results/invocations/${KOKORO_BUILD_ID}"
echo "KOKORO_SPONGE_URL http://sponge.corp.google.com/${KOKORO_BUILD_ID}"

# poor man's urlencode
ENCODED_KOKORO_JOB_NAME=$(echo "${KOKORO_JOB_NAME}" | sed 's|/|%2F|g')
# Fusion UI has the button for triggering/restarting builds.
echo "KOKORO_FUSION_URL http://fusion2.corp.google.com/ci/kokoro/prod:${ENCODED_KOKORO_JOB_NAME}/activity/${KOKORO_BUILD_ID}"

echo "KOKORO_BUILD_NUMBER ${KOKORO_BUILD_NUMBER}"
echo "KOKORO_JOB_NAME ${KOKORO_JOB_NAME}"
echo "KOKORO_GITHUB_COMMIT ${KOKORO_GITHUB_COMMIT}"
