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

set -ex

shopt -s nullglob

GCS_ROOT=gs://packages.grpc.io
MANIFEST_FILE=index.xml
ARCHIVE_UUID=${KOKORO_BUILD_ID:-$(uuidgen)}
GIT_BRANCH_NAME=master #${KOKORO_GITHUB_COMMIT:-master}
GIT_COMMIT=${KOKORO_GIT_COMMIT:-unknown}
ARCHIVE_TIMESTAMP=$(date -Iseconds)
TARGET_DIR=$(mktemp -d grpc_publish_packages.sh.XXXX)
YEAR_MONTH_PREFIX=$(date "+%Y/%m")
YEAR_PREFIX=${YEAR_MONTH_PREFIX%%/*}
UPLOAD_ROOT=$TARGET_DIR/$YEAR_PREFIX
RELATIVE_PATH=$YEAR_MONTH_PREFIX/$ARCHIVE_UUID
BUILD_ROOT=$TARGET_DIR/$RELATIVE_PATH

LINUX_PACKAGES=$KOKORO_GFILE_DIR/github/grpc/artifacts
WINDOWS_PACKAGES=$KOKORO_GFILE_DIR/github/grpc/artifacts
# TODO(mmx): enable linux_extra
# LINUX_EXTRA_PACKAGES=$KOKORO_GFILE_DIR/github/grpc/artifacts

PYTHON_PACKAGES=(
  "$LINUX_PACKAGES"/grpcio-[0-9]*.whl
  "$LINUX_PACKAGES"/grpcio-[0-9]*.tar.gz
  "$LINUX_PACKAGES"/grpcio_tools-[0-9]*.whl
  "$LINUX_PACKAGES"/grpcio-tools-[0-9]*.tar.gz
  "$LINUX_PACKAGES"/grpcio-health-checking-[0-9]*.tar.gz
  "$LINUX_PACKAGES"/grpcio-reflection-[0-9]*.tar.gz
  "$LINUX_PACKAGES"/grpcio-testing-[0-9]*.tar.gz
  #"$LINUX_EXTRA_PACKAGES"/grpcio-[0-9]*.whl
  #"$LINUX_EXTRA_PACKAGES"/grpcio_tools-[0-9]*.whl
)

PHP_PACKAGES=(
  "$LINUX_PACKAGES"/grpc-[0-9]*.tgz
)

RUBY_PACKAGES=(
  "$LINUX_PACKAGES"/grpc-[0-9]*.gem
  "$LINUX_PACKAGES"/grpc-tools-[0-9]*.gem
)

CSHARP_PACKAGES=(
  "$WINDOWS_PACKAGES"/csharp_nugets_windows_dotnetcli.zip
)

function add_to_manifest() {
  local xml_type=$1
  local xml_name
  xml_name=$(basename "$2")
  local xml_sha256
  xml_sha256=$(openssl sha256 -r "$2" | cut -d " " -f 1)
  cp "$2" "$BUILD_ROOT"
  echo "<artifact type='$xml_type' name='$xml_name' sha256='$xml_sha256' />"
}

mkdir -p "$BUILD_ROOT"

{
  cat <<EOF
<?xml version="1.0"?>
<?xml-stylesheet href="/web-assets/build.xsl" type="text/xsl"?>
EOF
  echo "<build id='$ARCHIVE_UUID' timestamp='$ARCHIVE_TIMESTAMP'>"
  echo "<metadata>"
  echo "<branch>$GIT_BRANCH_NAME</branch>"
  echo "<commit>$GIT_COMMIT</commit>"
  echo "</metadata><artifacts>"

  for pkg in "${PYTHON_PACKAGES[@]}"; do add_to_manifest python "$pkg"; done
  for pkg in "${CSHARP_PACKAGES[@]}"; do add_to_manifest csharp "$pkg"; done
  for pkg in "${PHP_PACKAGES[@]}"; do add_to_manifest php "$pkg"; done
  for pkg in "${RUBY_PACKAGES[@]}"; do add_to_manifest ruby "$pkg"; done

  echo "</artifacts></build>"
}> "$BUILD_ROOT/$MANIFEST_FILE"

BUILD_XML_SHA=$(openssl sha256 -r "$BUILD_ROOT/$MANIFEST_FILE" | cut -d " " -f 1)

PREV_HOME=$(mktemp old-XXXXX-$MANIFEST_FILE)
NEW_HOME=$(mktemp new-XXXXX-$MANIFEST_FILE)
gsutil cp "$GCS_ROOT/$MANIFEST_FILE" "$PREV_HOME"

{
  head --lines=4 "$PREV_HOME"
  echo "<build id='$ARCHIVE_UUID' timestamp='$ARCHIVE_TIMESTAMP' branch='$GIT_BRANCH_NAME' commit='$GIT_COMMIT' manifest='archive/$RELATIVE_PATH/$MANIFEST_FILE' manifest-sha256='$BUILD_XML_SHA' />"
  tail --lines=+5 "$PREV_HOME"
}> "$NEW_HOME"

gsutil -m cp -r "$UPLOAD_ROOT" "$GCS_ROOT/archive"
gsutil -h "Content-Type:application/xml" cp "$NEW_HOME" "$GCS_ROOT/$MANIFEST_FILE"

