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

set -euo pipefail

cd "$(dirname "$0")"

GCS_WEB_ASSETS="gs://packages.grpc.io/web-assets/"

REVERSE=0
while (( $# > 0 )); do
  case "$1" in
    -r|--reverse)
      REVERSE=1
      shift
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

SYNC_WEB_ASSETS=(
  404.html
  build-201807.xsl
  dirindex.css
  home.xsl
  style.css
)


list_remote_web_assets() {
  # Note this lists assets outside of SYNC_WEB_ASSETS to know if we
  # need to download something else
  gcloud storage hash --skip-crc32c --hex --format="table(md5_hash, url)" \
    "${GCS_WEB_ASSETS}*"
  echo
}

list_local_web_assets() {
  md5sum "${SYNC_WEB_ASSETS[@]}"
  echo
}

main() {
  echo "${GCS_WEB_ASSETS} path contents:"
  list_remote_web_assets

  echo "Local assets:"
  list_local_web_assets

  # GCS -> local sync.
  if (( REVERSE )); then
    set -x
    gcloud storage cp "${SYNC_WEB_ASSETS[@]/#/${GCS_WEB_ASSETS}}" .
    { set +x; } 2>/dev/null

    echo
    echo "Local assets after download:"
    list_local_web_assets
    return
  fi

  # Local -> GCS sync (guarded with a prompt).
  read -r -p "Upload web assets to ${GCS_WEB_ASSETS} [yN] " response
  if [[ ! "${response}" =~ ^[yY]$ ]]; then
    echo "Cancelled." >&2
    return 1
  fi

  set -x
  gcloud storage cp "${SYNC_WEB_ASSETS[@]}" "${GCS_WEB_ASSETS}"
  { set +x; } 2>/dev/null

  echo
  echo "${GCS_WEB_ASSETS} path contents after upload:"
  list_remote_web_assets
}

main
