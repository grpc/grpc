#!/bin/bash

# Script to upload github archives for bazel dependencies to GCS, creating a reliable mirror link.
# Archives are copied to "grpc-bazel-mirror" GCS bucket (https://console.cloud.google.com/storage/browser/grpc-bazel-mirror?project=grpc-testing)
# and will by downloadable with the https://storage.googleapis.com/grpc-bazel-mirror/ prefix.

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

  echo "Downloading https://${file}"
  curl -L --fail --output "${tmpdir}/archive" "https://${file}"

  echo "Uploading https://${file} to https://storage.googleapis.com/grpc-bazel-mirror/${file}"
  gsutil cp -n "${tmpdir}/archive" "gs://grpc-bazel-mirror/${file}"  # "-n" will skip existing files

  rm -rf "${tmpdir}/archive"
}

# How to check that all mirror URLs work:
# 1. clean $HOME/.cache/bazel
# 2. bazel clean --expunge
# 3. bazel sync (failed downloads will print warnings)

# A specific link can be upload manually by running e.g.
# upload "github.com/google/boringssl/archive/1c2769383f027befac5b75b6cedd25daf3bf4dcf.tar.gz"

# Collect the github archives to mirror from grpc_deps.bzl
grep -o '"https://github.com/[^"]*"' bazel/grpc_deps.bzl | sed 's/^"https:\/\///' | sed 's/"$//' | while read -r line ; do
    echo "Updating mirror for ${line}"
    upload "${line}"
done
