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

INPUT_ARTIFACTS=$KOKORO_GFILE_DIR/github/grpc/artifacts
INDEX_FILENAME=index.xml

BUILD_ID=${KOKORO_BUILD_ID:-$(uuidgen)}
BUILD_BRANCH_NAME=master
BUILD_GIT_COMMIT=${KOKORO_GIT_COMMIT:-unknown}
BUILD_TIMESTAMP=$(date -Iseconds)
BUILD_RELPATH=$(date "+%Y/%m")/$BUILD_ID/

GCS_ROOT=gs://packages.grpc.io/
GCS_ARCHIVE_PREFIX=archive/
GCS_ARCHIVE_ROOT=$GCS_ROOT$GCS_ARCHIVE_PREFIX
GCS_INDEX=$GCS_ROOT$INDEX_FILENAME

LOCAL_STAGING_TEMPDIR=$(mktemp -d)
LOCAL_BUILD_ROOT=$LOCAL_STAGING_TEMPDIR/$BUILD_RELPATH
LOCAL_BUILD_INDEX=$LOCAL_BUILD_ROOT$INDEX_FILENAME

mkdir -p "$LOCAL_BUILD_ROOT"

find "$INPUT_ARTIFACTS" -type f

# protoc Plugins
PROTOC_PLUGINS_ZIPPED_PACKAGES=$(mktemp -d)
for zip_dir in protoc_windows_{x86,x64}
do
  zip -jr "$PROTOC_PLUGINS_ZIPPED_PACKAGES/$zip_dir.zip" "$INPUT_ARTIFACTS/$zip_dir/"*
done
for tar_dir in protoc_{linux,macos}_{x86,x64}
do
  chmod +x "$INPUT_ARTIFACTS/$tar_dir"/*
  tar -cvzf "$PROTOC_PLUGINS_ZIPPED_PACKAGES/$tar_dir.tar.gz" -C "$INPUT_ARTIFACTS/$tar_dir" .
done

PROTOC_PACKAGES=(
  "$PROTOC_PLUGINS_ZIPPED_PACKAGES"/protoc_windows_{x86,x64}.zip
  "$PROTOC_PLUGINS_ZIPPED_PACKAGES"/protoc_{linux,macos}_{x86,x64}.tar.gz
)

# C#
UNZIPPED_CSHARP_PACKAGES=$(mktemp -d)
unzip "$INPUT_ARTIFACTS/csharp_nugets_windows_dotnetcli.zip" -d "$UNZIPPED_CSHARP_PACKAGES"
CSHARP_PACKAGES=(
  "$UNZIPPED_CSHARP_PACKAGES"/*
)

# Python
PYTHON_PACKAGES=(
  "$INPUT_ARTIFACTS"/grpcio-[0-9]*.tar.gz
  "$INPUT_ARTIFACTS"/grpcio-[0-9]*.whl
  "$INPUT_ARTIFACTS"/python_linux_extra_arm*/grpcio-[0-9]*.whl

  "$INPUT_ARTIFACTS"/grpcio-tools-[0-9]*.tar.gz
  "$INPUT_ARTIFACTS"/grpcio_tools-[0-9]*.whl
  "$INPUT_ARTIFACTS"/python_linux_extra_arm*/grpcio_tools-[0-9]*.whl

  "$INPUT_ARTIFACTS"/grpcio-health-checking-[0-9]*.tar.gz
  "$INPUT_ARTIFACTS"/grpcio-reflection-[0-9]*.tar.gz
  "$INPUT_ARTIFACTS"/grpcio-testing-[0-9]*.tar.gz
)

# PHP
PHP_PACKAGES=(
  "$INPUT_ARTIFACTS"/grpc-[0-9]*.tgz
)

# Ruby
RUBY_PACKAGES=(
  "$INPUT_ARTIFACTS"/grpc-[0-9]*.gem
  "$INPUT_ARTIFACTS"/grpc-tools-[0-9]*.gem
)

function add_to_manifest() {
  local artifact_type=$1
  local artifact_file=$2
  local artifact_name
  artifact_name=$(basename "$artifact_file")
  local artifact_sha256
  artifact_sha256=$(openssl sha256 -r "$artifact_file" | cut -d " " -f 1)
  local artifact_target=$LOCAL_BUILD_ROOT/$artifact_type
  mkdir -p "$artifact_target"
  cp "$artifact_file" "$artifact_target"
  cat <<EOF
    <artifact name='$artifact_name'
              type='$artifact_type'
              path='$artifact_type/$artifact_name'
              sha256='$artifact_sha256' />
EOF
}

{
  cat <<EOF
<?xml version="1.0"?>
<?xml-stylesheet href="/web-assets/build-201807.xsl" type="text/xsl"?>
<build id='$BUILD_ID' timestamp='$BUILD_TIMESTAMP' version="201807">
  <metadata>
    <project>gRPC</project>
    <repository>https://github.com/grpc/grpc</repository>
    <branch>$BUILD_BRANCH_NAME</branch>
    <commit>$BUILD_GIT_COMMIT</commit>
  </metadata>
  <artifacts>
EOF

  for pkg in "${PROTOC_PACKAGES[@]}"; do add_to_manifest protoc "$pkg"; done
  for pkg in "${CSHARP_PACKAGES[@]}"; do add_to_manifest csharp "$pkg"; done
  for pkg in "${PHP_PACKAGES[@]}"; do add_to_manifest php "$pkg"; done
  for pkg in "${PYTHON_PACKAGES[@]}"; do add_to_manifest python "$pkg"; done
  for pkg in "${RUBY_PACKAGES[@]}"; do add_to_manifest ruby "$pkg"; done

  cat <<EOF
  </artifacts>
</build>
EOF
}> "$LOCAL_BUILD_INDEX"

LOCAL_BUILD_INDEX_SHA256=$(openssl sha256 -r "$LOCAL_BUILD_INDEX" | cut -d " " -f 1)

OLD_INDEX=$(mktemp)
NEW_INDEX=$(mktemp)

# Download the current /index.xml into $OLD_INDEX
gsutil cp "$GCS_INDEX" "$OLD_INDEX"

{
  # we want to add an entry as the first child under <builds> tag
  # we can get by without a real XML parser by rewriting the header,
  # injecting our new tag, and then dumping the rest of the file as is.
  cat <<EOF
<?xml version="1.0"?>
<?xml-stylesheet href="/web-assets/home.xsl" type="text/xsl"?>
<packages>
  <builds>
    <build id='$BUILD_ID'
           timestamp='$BUILD_TIMESTAMP'
           branch='$BUILD_BRANCH_NAME'
           commit='$BUILD_GIT_COMMIT'
           path='$GCS_ARCHIVE_PREFIX$BUILD_RELPATH$INDEX_FILENAME'
           sha256='$LOCAL_BUILD_INDEX_SHA256' />
EOF
  tail --lines=+5 "$OLD_INDEX"
}> "$NEW_INDEX"

# Upload the current build artifacts
gsutil -m cp -r "$LOCAL_STAGING_TEMPDIR/${BUILD_RELPATH%%/*}" "$GCS_ARCHIVE_ROOT"
# Upload the new /index.xml
gsutil -h "Content-Type:application/xml" cp "$NEW_INDEX" "$GCS_INDEX"
