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

set -ex

cd "$(dirname "$0")/../../.."

# Set up workspace for grpc-rust.
RUST_WORKSPACE=$(pwd)/../rust_workspace
rm -rf "$RUST_WORKSPACE"

# Clone the sibling grpc-rust repo to avoid dirtying it.
git clone ../grpc-rust "$RUST_WORKSPACE"

# Build the worker.
(cd "$RUST_WORKSPACE" && cargo build -p grpc-benchmark --release --bin worker)

# Copy the binary to a clean location.
mkdir -p ../rust_bin
cp "$RUST_WORKSPACE/target/release/worker" ../rust_bin/worker
