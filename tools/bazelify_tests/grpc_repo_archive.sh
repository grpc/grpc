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

ARCHIVE_WO_SUBMODULES="$1"
ARCHIVE_WITH_SUBMODULES="$2"
export ARCHIVES_DIR="$(pwd)/archives"

export ARCHIVE_FORMAT=tar

mkdir -p "${ARCHIVES_DIR}"
rm -rf "${ARCHIVES_DIR}"/*

# TODO(jtattermusch): This script is currently only tested on linux.
# Nothing prevents it from working on other systems in principle,
# but more work is needed.

# HACK: To be able to collect all grpc source files as an archive
# we need to break from bazel's "sandbox" to be able to read files
# from the original bazel workspace (which in our case is the grpc repository root)
# This action runs with the "standalone" (a.k.a) local strategy,
# so path to the original bazel workspace from where this was invoked
# can be obtained by resolving the link that points to one of the
# source files.
# 1. find first component of the relative path to this script
# 2. resolve the symlink (it will point to same dir in the workspace)
# 3. one level up is the root of the original bazel workspace
FIRST_PATH_COMPONENT="$(echo $0 | sed 's|/.*||')"
ORIGINAL_BAZEL_WORKSPACE_ROOT="$(dirname $(readlink ${FIRST_PATH_COMPONENT}))"

# extract STABLE_GIT_COMMIT from stable-status.txt
GRPC_GIT_COMMIT_FROM_STABLE_STATUS=$(grep ^STABLE_GRPC_GIT_COMMIT bazel-out/stable-status.txt | cut -d' ' -f2)

if [ "${GRPC_GIT_COMMIT_FROM_STABLE_STATUS}" == "" ]
then
  echo "Failed to obtain info about gRPC git commit from the bazel workspace. Make sure you invoke bazel with --workspace_status_command=tools/bazelify_tests/workspace_status_cmd.sh" >&2
  exit 1
fi

GRPC_UNCOMMITED_PATCH_CHECKSUM_FROM_STABLE_STATUS=$(grep ^STABLE_GRPC_UNCOMMITED_PATCH_CHECKSUM bazel-out/stable-status.txt | cut -d' ' -f2)
GRPC_GIT_WORKSPACE_DIRTY_FROM_STABLE_STATUS=$(grep ^STABLE_GRPC_GIT_WORKSPACE_DIRTY bazel-out/stable-status.txt | cut -d' ' -f2)

pushd ${ORIGINAL_BAZEL_WORKSPACE_ROOT} >/dev/null

if [ "${GRPC_GIT_COMMIT_FROM_STABLE_STATUS}" != "$(git rev-parse HEAD)" ]
then
  echo "Info about gRPC commit from stable-status.txt doesn't match the commit at which the workspace root is." >&2
  echo "This is unexpected and giving up early is better than risking to end up with bogus results." >&2
  exit 1
fi

mkdir -p ${ARCHIVES_DIR}/grpc
git archive --format="${ARCHIVE_FORMAT}" HEAD >"${ARCHIVES_DIR}/grpc/$(git rev-parse HEAD).${ARCHIVE_FORMAT}"

if [ "${GRPC_UNCOMMITED_PATCH_CHECKSUM_FROM_STABLE_STATUS}" != "" ]
then
  git diff HEAD >"${ARCHIVES_DIR}/grpc/grpc_uncommited_${GRPC_UNCOMMITED_PATCH_CHECKSUM_FROM_STABLE_STATUS}.patch"
  # check that the actual checksum of the patch file is what we expect it to be
  echo "${GRPC_UNCOMMITED_PATCH_CHECKSUM_FROM_STABLE_STATUS} ${ARCHIVES_DIR}/grpc/grpc_uncommited_${GRPC_UNCOMMITED_PATCH_CHECKSUM_FROM_STABLE_STATUS}.patch" | sha256sum --quiet --check
fi

# produce archive for each submodule 
git submodule --quiet foreach 'git_commit="$(git rev-parse HEAD)"; mkdir -p ${ARCHIVES_DIR}/${name}; git archive --format=${ARCHIVE_FORMAT} HEAD >${ARCHIVES_DIR}/${name}/${git_commit}.${ARCHIVE_FORMAT}'

popd >/dev/null

# Extract grpc
mkdir -p grpc
tar -xopf "${ARCHIVES_DIR}/grpc/${GRPC_GIT_COMMIT_FROM_STABLE_STATUS}.${ARCHIVE_FORMAT}" -C grpc

# apply the patch
if [ "${GRPC_UNCOMMITED_PATCH_CHECKSUM_FROM_STABLE_STATUS}" != "" ]
then
  pushd grpc >/dev/null
  patch --quiet -p1 <"${ARCHIVES_DIR}/grpc/grpc_uncommited_${GRPC_UNCOMMITED_PATCH_CHECKSUM_FROM_STABLE_STATUS}.patch"
  popd >/dev/null
fi

# The archive produced need to be deterministic/stable.
# Passing the following args to tar should be enough to make them so.
# See https://reproducible-builds.org/docs/archives/
DETERMINISTIC_TAR_ARGS=(
  --sort=name
  # use a fixed mtime timestamp for all files (2015-01-01 00:00Z works just fine)
  --mtime="@1420070400"
  --owner=0
  --group=0
  --numeric-owner
  --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime
)

# create archive without submodules first
tar "${DETERMINISTIC_TAR_ARGS[@]}" -czf ${ARCHIVE_WO_SUBMODULES} grpc

SUBMODULE_ARCHIVE_LIST="$(grep 'STABLE_GRPC_SUBMODULE_ARCHIVES ' bazel-out/stable-status.txt | sed 's/^STABLE_GRPC_SUBMODULE_ARCHIVES //')"
# TODO(jtattermusch): handle spaces in submodule directory path
for submodule_archive in ${SUBMODULE_ARCHIVE_LIST}
do 
 archive_subdir="grpc/$(dirname ${submodule_archive})"
 mkdir -p $archive_subdir
 # Extract submodule archive under the correct subdirectory in grpc
 tar -xopf "${ARCHIVES_DIR}/${submodule_archive}.${ARCHIVE_FORMAT}" -C $archive_subdir
done

# create archive with everything
tar "${DETERMINISTIC_TAR_ARGS[@]}" -czf ${ARCHIVE_WITH_SUBMODULES} grpc

# Cleanup intermediate files we created
rm -rf "${ARCHIVES_DIR}" grpc
