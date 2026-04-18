#!/bin/bash
# Copyright 2026 gRPC authors.
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

if [ "$#" -lt 1 ] || [ "$1" = "--help" ]; then
  echo "Usage: $0 <bazel_target> [output_directory]"
  echo "Example: $0 //test/core/util:sorted_pack_test /tmp/grpc_traces"
  exit 1
fi

TARGET="$1"
OUTPUT_DIR="${2:-/tmp/grpc_traces}"

echo "--- 1. Building target $TARGET with Clang profiling enabled ---"
# Inject a unique define to bypass action caching
UNIQUE_TIMESTAMP=$(date +%s)

# Ensure we run from workspace root
cd "$(dirname "$0")/../.."

bazel build \
    --copt=-ftime-trace \
    --spawn_strategy=local \
    --copt=-D_FORCE_PROFILE_TIMESTAMP="$UNIQUE_TIMESTAMP" \
    "$TARGET"

echo "--- 2. Extracting trace files to $OUTPUT_DIR ---"
mkdir -p "$OUTPUT_DIR"

# Find all resulting profile files under bazel-out
find bazel-out/ -type f -name "*.json" -not -name "*.compile_commands.json" | while read -r file; do
    base_name=$(basename "$file")
    # Ensure uniqueness when copying multiple files by retaining part of the directory path
    # For simpler use, we just copy them with their basename if there are no conflicts
    cp "$file" "$OUTPUT_DIR/$base_name"
    echo "Copied: $OUTPUT_DIR/$base_name"
done

echo "--- Done! Traces are located in $OUTPUT_DIR ---"
