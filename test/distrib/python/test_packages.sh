#!/usr/bin/env bash
# Copyright 2018 gRPC authors.
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

cd "$(dirname "$0")"

shopt -s nullglob

echo "Testing Python packages with input artifacts:"
ls "$EXTERNAL_GIT_ROOT"/input_artifacts

# Debug: Show what's in each subdirectory
echo "DEBUG: Contents of input_artifacts subdirectories:"
for dir in "$EXTERNAL_GIT_ROOT"/input_artifacts/*/; do
  if [ -d "$dir" ]; then
    echo "DEBUG: Directory: $dir"
    ls -la "$dir" || echo "DEBUG: Failed to list $dir"
  fi
done

# Debug: Show what's in the artifacts directory
echo "DEBUG: Contents of artifacts directory:"
ls -la "$EXTERNAL_GIT_ROOT"/artifacts/ || echo "DEBUG: Failed to list artifacts directory"

# Debug: Show what's in each artifacts subdirectory
echo "DEBUG: Contents of artifacts subdirectories:"
for dir in "$EXTERNAL_GIT_ROOT"/artifacts/*/; do
  if [ -d "$dir" ]; then
    echo "DEBUG: Artifacts Directory: $dir"
    ls -la "$dir" || echo "DEBUG: Failed to list $dir"
  fi
done

if [[ "$1" == "binary" ]]
then
  echo "Testing Python binary distribution"
  # Look for wheel files in both the root input_artifacts directory and subdirectories
  echo "DEBUG: Looking for wheel files with patterns:"
  echo "DEBUG: Pattern 1: $EXTERNAL_GIT_ROOT/input_artifacts/grpcio[-_0-9a-z.]*.whl"
  echo "DEBUG: Pattern 2: $EXTERNAL_GIT_ROOT/input_artifacts/*/grpcio[-_0-9a-z.]*.whl"
  
  ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[-_0-9a-z.]*.whl "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[-_0-9a-z.]*.whl "$EXTERNAL_GIT_ROOT"/artifacts/*/grpcio[-_0-9a-z.]*.whl)
  TOOLS_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*tools[-_0-9a-z.]*.whl "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[_-]*tools[-_0-9a-z.]*.whl "$EXTERNAL_GIT_ROOT"/artifacts/*/grpcio[_-]*tools[-_0-9a-z.]*.whl)
  OBSERVABILITY_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*observability[-_0-9a-z.]*.whl "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[_-]*observability[-_0-9a-z.]*.whl "$EXTERNAL_GIT_ROOT"/artifacts/*/grpcio[_-]*observability[-_0-9a-z.]*.whl)
  
  # Debug: Show what files were found
  echo "DEBUG: ARCHIVES array contains: ${#ARCHIVES[@]} files"
  for file in "${ARCHIVES[@]}"; do
    echo "DEBUG: Found archive: $file"
  done
  
  echo "DEBUG: TOOLS_ARCHIVES array contains: ${#TOOLS_ARCHIVES[@]} files"
  for file in "${TOOLS_ARCHIVES[@]}"; do
    echo "DEBUG: Found tools archive: $file"
  done
  
  echo "DEBUG: OBSERVABILITY_ARCHIVES array contains: ${#OBSERVABILITY_ARCHIVES[@]} files"
  for file in "${OBSERVABILITY_ARCHIVES[@]}"; do
    echo "DEBUG: Found observability archive: $file"
  done
  
  # Debug: Test the glob patterns directly
  echo "DEBUG: Testing glob patterns directly:"
  echo "DEBUG: Pattern 1 results:"
  ls "$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[-_0-9a-z.]*.whl 2>/dev/null || echo "DEBUG: Pattern 1 found no files"
  echo "DEBUG: Pattern 2 results:"
  ls "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[-_0-9a-z.]*.whl 2>/dev/null || echo "DEBUG: Pattern 2 found no files"
  echo "DEBUG: Pattern 3 results:"
  ls "$EXTERNAL_GIT_ROOT"/artifacts/*/grpcio[-_0-9a-z.]*.whl 2>/dev/null || echo "DEBUG: Pattern 3 found no files"
else
  echo "Testing Python source distribution"
  # Look for source files in both the root input_artifacts directory and subdirectories
  ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[-_0-9a-z.]*.tar.gz "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[-_0-9a-z.]*.tar.gz)
  TOOLS_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*tools[-_0-9a-z.]*.tar.gz "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[_-]*tools[-_0-9a-z.]*.tar.gz)
  OBSERVABILITY_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*observability[-_0-9a-z.]*.tar.gz "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[_-]*observability[-_0-9a-z.]*.tar.gz)
fi

HEALTH_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*health[_-]*checking[-_0-9a-z.]*.tar.gz "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[_-]*health[_-]*checking[-_0-9a-z.]*.tar.gz)
REFLECTION_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*reflection[-_0-9a-z.]*.tar.gz "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[_-]*reflection[-_0-9a-z.]*.tar.gz)
TESTING_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*testing[-_0-9a-z.]*.tar.gz "$EXTERNAL_GIT_ROOT"/input_artifacts/*/grpcio[_-]*testing[-_0-9a-z.]*.tar.gz)

VIRTUAL_ENV=$(mktemp -d)
python3 -m virtualenv "$VIRTUAL_ENV"
PYTHON=$VIRTUAL_ENV/bin/python

"$PYTHON" -m pip install --upgrade six pip wheel setuptools

function validate_wheel_hashes() {
  for file in "$@"; do
    "$PYTHON" -m wheel unpack "$file" -d /tmp || return 1
  done
  return 0
}

function at_least_one_installs() {
  echo "DEBUG: at_least_one_installs called with $# arguments"
  for file in "$@"; do
    echo "DEBUG: Attempting to install: $file"
    # Use --no-index --find-links to install from local artifacts instead of PyPI
    # This prevents dependency resolution issues with development versions
    if "$PYTHON" -m pip install --no-index --find-links "$EXTERNAL_GIT_ROOT"/input_artifacts/ --find-links "$EXTERNAL_GIT_ROOT"/artifacts/ "$file"; then
      echo "DEBUG: Successfully installed: $file"
      return 0
    else
      echo "DEBUG: Failed to install: $file"
    fi
  done
  echo "DEBUG: All installations failed"
  return 1
}


#
# Validate the files in wheel matches their hashes and size in RECORD
#

if [[ "$1" == "binary" ]]; then
  validate_wheel_hashes "${ARCHIVES[@]}"
  validate_wheel_hashes "${TOOLS_ARCHIVES[@]}"
  validate_wheel_hashes "${OBSERVABILITY_ARCHIVES[@]}"
fi


#
# Install our distributions in order of dependencies
#

at_least_one_installs "${ARCHIVES[@]}"
at_least_one_installs "${TOOLS_ARCHIVES[@]}"
at_least_one_installs "${HEALTH_ARCHIVES[@]}"
at_least_one_installs "${REFLECTION_ARCHIVES[@]}"
at_least_one_installs "${TESTING_ARCHIVES[@]}"
at_least_one_installs "${OBSERVABILITY_ARCHIVES[@]}"


#
# Test our distributions
#

# TODO(jtattermusch): add a .proto file to the distribtest, generate python
# code from it and then use the generated code from distribtest.py
"$PYTHON" -m grpc_tools.protoc --help

"$PYTHON" distribtest.py
