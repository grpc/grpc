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

GIT_ROOT="$(dirname "$0")/../../.."
cd "${GIT_ROOT}"

# This Bazel log capture script is based on the internal version and corrected
# with the OSS command behavior, latest Sponge behavior, and Kokoro behavior.
# https://g3doc.corp.google.com/devtools/kokoro/g3doc/userdocs/general/custom_test_logs.md#converting-bazel-test-logs-to-spongeresultstore-test-logs
capture_bazel_logs() {
  mkdir -p "$KOKORO_ARTIFACTS_DIR"
  # Copy all logs and xmls to the kokoro artifacts directory
  (cd "./bazel-testlogs" &&
      find -L . -name "test.log" -exec cp --parents {} "$KOKORO_ARTIFACTS_DIR" \; &&
      find -L . -name "test.xml" -exec cp --parents {} "$KOKORO_ARTIFACTS_DIR" \;)
  # Rename the copied test.log and test.xml files to sponge_log.log and sponge_log.xml
  find -L "$KOKORO_ARTIFACTS_DIR" -name "test.log" | sed -e "p;s/test.log/sponge_log.log/" | xargs -n2 mv
  find -L "$KOKORO_ARTIFACTS_DIR" -name "test.xml" | sed -e "p;s/test.xml/sponge_log.xml/" | xargs -n2 mv
}

trap capture_bazel_logs EXIT

# Cloud Build requires newer gcloud to understand the machineType option.
gcloud components update --quiet

export CC=/usr/bin/gcc
tools/bazel test --test_output=errors --action_env=HOME=$(echo $HOME) tools/run_tests/dockerize/xds/...
