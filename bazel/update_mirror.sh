#!/bin/bash
# Copyright 2020 The gRPC Authors
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

# Script to upload github archives for bazel dependencies to GCS, creating a reliable mirror link.
# Archives are copied to "grpc-bazel-mirror" GCS bucket (https://console.cloud.google.com/storage/browser/grpc-bazel-mirror?project=grpc-testing)
# and will by downloadable with the https://storage.googleapis.com/grpc-bazel-mirror/ prefix.
#
# This script should be run each time bazel dependencies are updated.

set -e

cd $(dirname $0)/..

# Create a temp directory to hold the versioned tarball,
# and clean it up when the script exits.
tmpdir="$(mktemp -d)"
function cleanup {
  rm -rf "$tmpdir"
}
trap cleanup EXIT

function upload {
  local file="$1"

  if gsutil stat "gs://grpc-bazel-mirror/${file}" > /dev/null
  then
    echo "Skipping ${file}"
  else
    echo "Downloading https://${file}"
    curl -L --fail --output "${tmpdir}/archive" "https://${file}"

    echo "Uploading https://${file} to https://storage.googleapis.com/grpc-bazel-mirror/${file}"
    gsutil cp "${tmpdir}/archive" "gs://grpc-bazel-mirror/${file}"

    rm -rf "${tmpdir}/archive"
  fi
}

# How to check that all mirror URLs work:
# 1. clean $HOME/.cache/bazel
# 2. bazel clean --expunge
# 3. bazel sync (failed downloads will print warnings)

# A specific link can be upload manually by running e.g.
# upload "github.com/google/boringssl/archive/1c2769383f027befac5b75b6cedd25daf3bf4dcf.tar.gz"

# bazel binaries used by the tools/bazel wrapper script
upload github.com/bazelbuild/bazel/releases/download/5.4.1/bazel-5.4.1-linux-x86_64
upload github.com/bazelbuild/bazel/releases/download/5.4.1/bazel-5.4.1-darwin-x86_64
upload github.com/bazelbuild/bazel/releases/download/5.4.1/bazel-5.4.1-windows-x86_64.exe

upload github.com/bazelbuild/bazel/releases/download/6.1.2/bazel-6.1.2-linux-x86_64
upload github.com/bazelbuild/bazel/releases/download/6.1.2/bazel-6.1.2-darwin-x86_64
upload github.com/bazelbuild/bazel/releases/download/6.1.2/bazel-6.1.2-windows-x86_64.exe

# Collect the github archives to mirror from grpc_deps.bzl
grep -o '"https://github.com/[^"]*"' bazel/grpc_deps.bzl | sed 's/^"https:\/\///' | sed 's/"$//' | while read -r line ; do
    echo "Updating mirror for ${line}"
    upload "${line}"
done
