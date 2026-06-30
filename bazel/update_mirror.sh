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

PS4='+ $(date "+[%H:%M:%S %Z]") $LINENO:\011'
set -ex

cd $(dirname $0)/..

# Create a temp directory to hold the versioned tarball,
# and clean it up when the script exits.
tmpdir="$(mktemp -d)"
archive_dir=${tmpdir}/archives
mkdir -p ${archive_dir}

success=0
failure=0
function cleanup {
  echo "stats: Attempted to upload $((success+failure)) files in total, ${success} succeeded, ${failure} failed."
  rm -rf "$tmpdir"
}
trap cleanup EXIT

function _download() {
  local uri="$1"
  # The relative path of gcs object.
  local obj_path="${uri#https://}"

  if [[ "$obj_path" == sourceforge.net/*/download ]]; then
    obj_path="${obj_path%/download}"
  fi

  local local_path="${archive_dir}/${obj_path}" 

  echo "Downloading ${uri}"
  curl -L --fail --create-dirs --output "${local_path}" "${uri}"

  if [[ ! -s "${local_path}" ]]; then
    echo "Failed to download ${uri}: zero bytes returned"
    return 1
  fi
}

# Wrapper of _download() which handles errors and keep track of stats.
function download() {
  local url="$1"
  if ! _download "${url}"; then
    echo "Failed to download url ${url}"
    failure=$((failure + 1))
  else
    success=$((success + 1))
  fi
}

function rsync_archives() {
  gcloud storage rsync --recursive "${archive_dir}/" gs://grpc-bazel-mirror/
}

# Download everything into a temporary folder and perform rsync in one go.
function upload_deps {
  local bzlmod_deps_file=${tmpdir}/bzlmod_deps.ndjson
  local output_file=${tmpdir}/urls_to_upload.txt
  local existing_archives_file=${tmpdir}/existing_archives.txt

  tools/bazel mod show_repo --all_repos --output=streamed_jsonproto > ${bzlmod_deps_file} || true
  gcloud storage objects list '--format=json(name, md5_hash)' 'gs://grpc-bazel-mirror/**' | jq '.[] | "https://" + .name ' -r > ${existing_archives_file}

  python3 \
    bazel/update_mirror_helper.py \
    --bzlmod_deps_file=${bzlmod_deps_file} \
    --existing_archives_file=${existing_archives_file} \
    --output_file=${output_file}

  while read -r url; do
      case "$url" in
          *github.com*)
            echo "Downloading archive from github.com: ${url}"
            download "${url}"
            ;;
          *)
            echo "Downloading archive from non-github site: ${url}"
            download "${url}"
            ;;
      esac
  done < "${output_file}"
  rsync_archives
}

upload_deps
