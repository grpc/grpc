#!/bin/bash
# Copyright 2023 The gRPC Authors
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

ARCHIVE_WITH_SUBMODULES="$1"
BUILD_SCRIPT="$2"
shift 2

# Extract grpc repo archive
tar -xopf ${ARCHIVE_WITH_SUBMODULES}
cd grpc

# Extract all input archives with artifacts into input_artifacts directory
# TODO(jtattermusch): Deduplicate the snippet below (it appears in multiple files).
mkdir -p input_artifacts
pushd input_artifacts >/dev/null
# all remaining args are .tar.gz archives with input artifacts
for input_artifact_archive in "$@"
do
  # extract the .tar.gz with artifacts into a directory named after a basename
  # of the archive itself (and strip the "artifact/" prefix)
  # Note that input artifacts from different dependencies can have files
  # with the same name, so disambiguating through the name of the archive
  # is important.
  archive_extract_dir="$(basename ${input_artifact_archive} .tar.gz)"
  mkdir -p "${archive_extract_dir}"
  pushd "${archive_extract_dir}" >/dev/null
  tar --strip-components=1 -xopf ../../../${input_artifact_archive}
  popd >/dev/null
done
popd >/dev/null

ls -lR input_artifacts

# Run build script passed as arg.
"${BUILD_SCRIPT}"
