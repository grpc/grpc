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

cd "$(dirname "$0")/../../.."

GRPC_VERSION=$(grep -e "^ *version: " build_handwritten.yaml | head -n 1 | sed 's/.*: //')

INPUT_ARTIFACTS=$KOKORO_GFILE_DIR/github/grpc/artifacts
INDEX_FILENAME=index.xml

BUILD_ID=${KOKORO_BUILD_ID:-$(uuidgen)}
BUILD_BRANCH_NAME=master
BUILD_GIT_COMMIT=${KOKORO_GIT_COMMIT:-unknown}
BUILD_TIMESTAMP=$(date -Iseconds)
BUILD_RELPATH=$(date "+%Y/%m")/$BUILD_GIT_COMMIT-$BUILD_ID/

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
  zip -jr "$PROTOC_PLUGINS_ZIPPED_PACKAGES/grpc-$zip_dir-$GRPC_VERSION.zip" "$INPUT_ARTIFACTS/$zip_dir/"*
done
for tar_dir in protoc_linux_{x86,x64} protoc_macos_x64
do
  chmod +x "$INPUT_ARTIFACTS/$tar_dir"/*
  tar -cvzf "$PROTOC_PLUGINS_ZIPPED_PACKAGES/grpc-$tar_dir-$GRPC_VERSION.tar.gz" -C "$INPUT_ARTIFACTS/$tar_dir" .
done

PROTOC_PACKAGES=(
  "$PROTOC_PLUGINS_ZIPPED_PACKAGES"/grpc-protoc_windows_{x86,x64}-"$GRPC_VERSION.zip"
  "$PROTOC_PLUGINS_ZIPPED_PACKAGES"/grpc-protoc_linux_{x86,x64}-"$GRPC_VERSION.tar.gz"
  "$PROTOC_PLUGINS_ZIPPED_PACKAGES"/grpc-protoc_macos_x64-"$GRPC_VERSION.tar.gz"
)

# C#
UNZIPPED_CSHARP_PACKAGES=$(mktemp -d)
# the "_multiplatform" suffix is to fix https://github.com/grpc/grpc/issues/32179
unzip "$INPUT_ARTIFACTS/csharp_nugets_windows_dotnetcli_multiplatform.zip" -d "$UNZIPPED_CSHARP_PACKAGES"
CSHARP_PACKAGES=(
  "$UNZIPPED_CSHARP_PACKAGES"/*
  "$INPUT_ARTIFACTS"/grpc_unity_package.[0-9]*.zip
)

# Python
PYTHON_GRPCIO_PACKAGES=(
  "$INPUT_ARTIFACTS"/grpcio-[0-9]*.tar.gz
  "$INPUT_ARTIFACTS"/grpcio-[0-9]*.whl
  "$INPUT_ARTIFACTS"/python_linux_extra_arm*/grpcio-[0-9]*.whl
)
PYTHON_GRPCIO_TOOLS_PACKAGES=(
  "$INPUT_ARTIFACTS"/grpcio-tools-[0-9]*.tar.gz
  "$INPUT_ARTIFACTS"/grpcio_tools-[0-9]*.whl
  "$INPUT_ARTIFACTS"/python_linux_extra_arm*/grpcio_tools-[0-9]*.whl
)
PYTHON_GRPCIO_HEALTH_CHECKING_PACKAGES=(
  "$INPUT_ARTIFACTS"/grpcio-health-checking-[0-9]*.tar.gz
)
PYTHON_GRPCIO_REFLECTION_PACKAGES=(
  "$INPUT_ARTIFACTS"/grpcio-reflection-[0-9]*.tar.gz
)
PYTHON_GRPCIO_TESTING_PACKAGES=(
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
  local artifact_prefix=$3
  local artifact_name
  artifact_name=$(basename "$artifact_file")
  local artifact_size
  artifact_size=$(stat -c%s "$artifact_file")
  local artifact_sha256
  artifact_sha256=$(openssl sha256 -r "$artifact_file" | cut -d " " -f 1)
  local artifact_target=$LOCAL_BUILD_ROOT/$artifact_type/$artifact_prefix
  mkdir -p "$artifact_target"
  cp "$artifact_file" "$artifact_target"
  cat <<EOF
    <artifact name='$artifact_name'
              type='$artifact_type'
              path='$artifact_type/$artifact_prefix$artifact_name'
              size='$artifact_size'
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
  for pkg in "${PYTHON_GRPCIO_PACKAGES[@]}"; do add_to_manifest python "$pkg" grpcio/; done
  for pkg in "${PYTHON_GRPCIO_TOOLS_PACKAGES[@]}"; do add_to_manifest python "$pkg" grpcio-tools/; done
  for pkg in "${PYTHON_GRPCIO_HEALTH_CHECKING_PACKAGES[@]}"; do add_to_manifest python "$pkg" grpcio-health-checking/; done
  for pkg in "${PYTHON_GRPCIO_REFLECTION_PACKAGES[@]}"; do add_to_manifest python "$pkg" grpcio-reflection/; done
  for pkg in "${PYTHON_GRPCIO_TESTING_PACKAGES[@]}"; do add_to_manifest python "$pkg" grpcio-testing/; done
  for pkg in "${RUBY_PACKAGES[@]}"; do add_to_manifest ruby "$pkg"; done

  cat <<EOF
  </artifacts>
</build>
EOF
}> "$LOCAL_BUILD_INDEX"

LOCAL_BUILD_INDEX_SIZE=$(stat -c%s "$LOCAL_BUILD_INDEX")
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
           size='$LOCAL_BUILD_INDEX_SIZE'
           sha256='$LOCAL_BUILD_INDEX_SHA256' />
EOF
  tail --lines=+5 "$OLD_INDEX"
}> "$NEW_INDEX"


function generate_directory_index()
{
  local target_dir=$1
  local current_directory_name
  current_directory_name=$(basename "$target_dir")
  cat <<EOF
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" lang="" xml:lang="">
  <head>
    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
    <title>Index of $current_directory_name - packages.grpc.io</title>
    <link rel="stylesheet" type="text/css" href="/web-assets/dirindex.css" />
  </head>
  <body>
    <h1>Index of <a href="#"><code>$current_directory_name</code></a></h1>
    <ul>
      <li><a href="#">.</a></li>
      <li><a href="..">..</a></li>
EOF

(
  cd "$target_dir"
  find * -maxdepth 0 -type d -print | sort | while read -r line
  do
    echo "      <li><a href='$line/'>$line/</a></li>"
  done
  find * -maxdepth 0 -type f -print | sort | while read -r line
  do
    echo "      <li><a href='$line'>$line</a></li>"
  done
)

cat <<EOF
    </ul>
  </body>
</html>
EOF
}

# Upload the current build artifacts
gsutil -m cp -r "$LOCAL_STAGING_TEMPDIR/${BUILD_RELPATH%%/*}" "$GCS_ARCHIVE_ROOT"
# Upload directory indices for subdirectories
(
  cd "$LOCAL_BUILD_ROOT"
  find * -type d | while read -r directory
  do
    generate_directory_index "$directory" | gsutil -h 'Content-Type:text/html' cp - "$GCS_ARCHIVE_ROOT$BUILD_RELPATH$directory/$INDEX_FILENAME"
  done
)
# Upload the new /index.xml
gsutil -h "Content-Type:application/xml" cp "$NEW_INDEX" "$GCS_INDEX"

# Upload C# nugets to the dev nuget feed
pushd "$UNZIPPED_CSHARP_PACKAGES"
docker pull mcr.microsoft.com/dotnet/core/sdk:2.1
for nugetfile in *.nupkg
do
  echo "Going to push $nugetfile"
  # use nuget from a docker container to push the nupkg
  set +x  # IMPORTANT: avoid revealing the nuget api key by the command echo
  docker run -v "$(pwd):/nugets:ro" --rm=true mcr.microsoft.com/dotnet/core/sdk:2.1 bash -c "dotnet nuget push /nugets/$nugetfile -k $(cat ${KOKORO_GFILE_DIR}/artifactory_grpc_nuget_dev_api_key) --source https://grpc.jfrog.io/grpc/api/nuget/v3/grpc-nuget-dev"
  set -ex
done
popd
