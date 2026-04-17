#!/usr/bin/env bash
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

set -ex

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# make sure bazel is available
tools/bazel version

./tools/run_tests/start_port_server.py

# for kokoro mac workers, exact image version is store in a well-known location on disk
KOKORO_IMAGE_VERSION="$(cat /VERSION)"

BAZEL_REMOTE_CACHE_ARGS=(
  # Enable uploading to remote cache. Requires the "roles/remotebuildexecution.actionCacheWriter" permission.
  --remote_upload_local_results=true
  # allow invalidating the old cache by setting to a new random key
  --remote_default_exec_properties="grpc_cache_silo_key1=83d8e488-1ca9-40fd-929e-d37d13529c99"
  # make sure we only get cache hits from binaries built on exact same macos image
  --remote_default_exec_properties="grpc_cache_silo_key2=${KOKORO_IMAGE_VERSION}"
)

# --- START OF BAZEL-DIFF INTEGRATION ---
FILTERED_TARGETS_PATH="/tmp/filtered_targets.txt"
if [ -n "$KOKORO_GITHUB_PULL_REQUEST_TARGET_BRANCH" ]; then
  echo "PR context detected: Using bazel-diff to determine impacted targets."
  curl -Lo /tmp/bazel-diff.jar https://github.com/Tinder/bazel-diff/releases/latest/download/bazel-diff_deploy.jar
  
  WORKSPACE_PATH=$(git rev-parse --show-toplevel)
  BAZEL_PATH="${WORKSPACE_PATH}/tools/bazel"
  PREVIOUS_REV="origin/$KOKORO_GITHUB_PULL_REQUEST_TARGET_BRANCH"
  FINAL_REV="HEAD"
  
  STARTING_HASHES_JSON="/tmp/starting_hashes.json"
  FINAL_HASHES_JSON="/tmp/final_hashes.json"
  IMPACTED_TARGETS_PATH="/tmp/impacted_targets.txt"

  # Stash any local changes (e.g. made by Kokoro setup) before switching branches
  git -C "$WORKSPACE_PATH" stash || true

  # Generate hashes for base revision
  git -C "$WORKSPACE_PATH" checkout "$PREVIOUS_REV" --quiet
  java -jar /tmp/bazel-diff.jar generate-hashes -w "$WORKSPACE_PATH" -b "$BAZEL_PATH" "$STARTING_HASHES_JSON"

  # Generate hashes for PR revision
  git -C "$WORKSPACE_PATH" checkout - --quiet # checkout previous branch (HEAD)
  java -jar /tmp/bazel-diff.jar generate-hashes -w "$WORKSPACE_PATH" -b "$BAZEL_PATH" "$FINAL_HASHES_JSON"

  # Restore stashed changes
  git -C "$WORKSPACE_PATH" stash pop || true

  # Get impacted targets
  java -jar /tmp/bazel-diff.jar get-impacted-targets -sh "$STARTING_HASHES_JSON" -fh "$FINAL_HASHES_JSON" -o "$IMPACTED_TARGETS_PATH" -w "$WORKSPACE_PATH"
  
  # Remove external targets and duplicates
  sort "$IMPACTED_TARGETS_PATH" | uniq | grep -v '^//external' > "$FILTERED_TARGETS_PATH" || true
  
  NUM_IMPACTED=$(wc -l < "$FILTERED_TARGETS_PATH" | tr -d ' ')
  # Calculate total targets for comparison
  TOTAL_TARGETS=$("${BAZEL_PATH}" query //... 2>/dev/null | wc -l | tr -d ' ')
  echo "[$NUM_IMPACTED/$TOTAL_TARGETS] Impacted targets found."
  TARGET_ARGS="--target_pattern_file=$FILTERED_TARGETS_PATH"
else
  TARGET_ARGS="-- //test/..."
fi
# --- END OF BAZEL-DIFF INTEGRATION ---

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_c_cpp_tests

# run all C/C++ tests
if [ -z "$KOKORO_GITHUB_PULL_REQUEST_TARGET_BRANCH" ] || [ -s "$FILTERED_TARGETS_PATH" ]; then
  bazel_c_cpp_tests/bazel_wrapper \
    --output_base=.bazel_rbe \
    --bazelrc=tools/remote_build/mac.bazelrc \
    test \
    --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
    "${BAZEL_REMOTE_CACHE_ARGS[@]}" \
    $BAZEL_FLAGS \
    ${TARGET_ARGS}
else
  echo "Skipping main bazel tests because bazel-diff reported no impacted targets."
fi

# run end2end tests with GRPC_CFSTREAM=1

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_c_cpp_cf_engine_tests

bazel_c_cpp_cf_engine_tests/bazel_wrapper \
  --output_base=.bazel_rbe \
  --bazelrc=tools/remote_build/mac.bazelrc \
  test \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  "${BAZEL_REMOTE_CACHE_ARGS[@]}" \
  $BAZEL_FLAGS \
  --cxxopt=-DGRPC_CFSTREAM=1 \
  --test_env=GRPC_EXPERIMENTS="event_engine_client,event_engine_listener" \
  --test_env=GRPC_TRACE="api,event_engine*" \
  -- \
  //test/core/end2end:bad_server_response_test \
  //test/core/end2end:connection_refused_test \
  //test/core/end2end:goaway_server_test \
  //test/core/end2end:invalid_call_argument_test \
  //test/core/end2end:multiple_server_queues_test \
  //test/core/end2end:no_server_test \
  //test/core/end2end:h2_ssl_cert_test \
  //test/core/end2end:h2_ssl_session_reuse_test \
  //test/core/end2end:h2_tls_peer_property_external_verifier_test \
  # //test/core/end2end:dualstack_socket_test uses iomgr \

