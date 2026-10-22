#!/bin/bash
# Copyright 2026 The gRPC Authors
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
#
# Builds Rust interop server and client in a base image.
set -ex

# build_interop_image.sh runs this script in a login shell (bash -l), and
# /etc/profile resets PATH, dropping the rust image's cargo directory.
export PATH="${CARGO_HOME:-/usr/local/cargo}/bin:${PATH}"

# grpc-rust does not check in Cargo.lock and its workspace uses resolver "2",
# so a fresh build would pick the newest crates, some of which need a newer
# rustc than the image's (the workspace MSRV). Prefer MSRV-compatible versions.
export CARGO_RESOLVER_INCOMPATIBLE_RUST_VERSIONS=fallback

git config --global --add safe.directory /var/local/jenkins/grpc-rust/.git
mkdir -p /var/local/git
git clone /var/local/jenkins/grpc-rust /var/local/git/grpc-rust

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

cd /var/local/git/grpc-rust
cargo build --release -p interop --bins --features protoc-gen-grpc-rust
