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

# Get number of parallel jobs from environment, default to number of CPUs
PARALLEL_JOBS=${GRPC_PYTHON_DISTRIBTEST_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}

echo "Testing Python packages with input artifacts:"
ls "$EXTERNAL_GIT_ROOT"/input_artifacts

if [[ "$1" == "binary" ]]
then
  echo "Testing Python binary distribution"
  ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[-_0-9a-z.]*.whl)
  TOOLS_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*tools[-_0-9a-z.]*.whl)
  OBSERVABILITY_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*observability[-_0-9a-z.]*.whl)
else
  echo "Testing Python source distribution"
  ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[-_0-9a-z.]*.tar.gz)
  TOOLS_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*tools[-_0-9a-z.]*.tar.gz)
  OBSERVABILITY_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*observability[-_0-9a-z.]*.tar.gz)
fi

HEALTH_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*health[_-]*checking[-_0-9a-z.]*.tar.gz)
REFLECTION_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*reflection[-_0-9a-z.]*.tar.gz)
TESTING_ARCHIVES=("$EXTERNAL_GIT_ROOT"/input_artifacts/grpcio[_-]*testing[-_0-9a-z.]*.tar.gz)

VIRTUAL_ENV=$(mktemp -d)
python3 -m virtualenv "$VIRTUAL_ENV"
PYTHON=$VIRTUAL_ENV/bin/python
# Create pip cache directory and enable pip cache for faster installations
mkdir -p /tmp/pip-cache
# Enable pip cache and use faster installation options
"$PYTHON" -m pip install --upgrade --cache-dir /tmp/pip-cache six pip==25.2 wheel setuptools

function validate_wheel_hashes() {
  local files=("$@")
  if [[ ${#files[@]} -eq 0 ]]; then
    return 0
  fi
  
  # Parallelize wheel hash validation
  local pids=()
  for file in "${files[@]}"; do
    (
      "$PYTHON" -m wheel unpack "$file" -d /tmp || exit 1
    ) &
    pids+=($!)
  done
  
  # Wait for all background jobs and check exit status
  local failed=0
  for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
      failed=1
    fi
  done
  
  return $failed
}

function at_least_one_installs() {
  for file in "$@"; do
    if "$PYTHON" -m pip install --cache-dir /tmp/pip-cache "$file"; then
      return 0
    fi
  done
  return 1
}

function install_packages_parallel() {
  # Install packages in parallel after grpcio is installed
  # All these packages only depend on grpcio, so they can be installed concurrently
  local pids=()
  
  # Install each package set in parallel
  (
    at_least_one_installs "${TOOLS_ARCHIVES[@]}" || exit 1
  ) &
  pids+=($!)
  
  (
    at_least_one_installs "${HEALTH_ARCHIVES[@]}" || exit 1
  ) &
  pids+=($!)
  
  (
    at_least_one_installs "${REFLECTION_ARCHIVES[@]}" || exit 1
  ) &
  pids+=($!)
  
  (
    at_least_one_installs "${TESTING_ARCHIVES[@]}" || exit 1
  ) &
  pids+=($!)
  
  (
    at_least_one_installs "${OBSERVABILITY_ARCHIVES[@]}" || exit 1
  ) &
  pids+=($!)
  
  # Wait for all background jobs and check exit status
  local failed=0
  for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
      failed=1
    fi
  done
  
  return $failed
}


#
# Validate the files in wheel matches their hashes and size in RECORD
#

if [[ "$1" == "binary" ]]; then
  # Parallelize wheel hash validation
  validate_wheel_hashes "${ARCHIVES[@]}" &
  local arch_pid=$!
  validate_wheel_hashes "${TOOLS_ARCHIVES[@]}" &
  local tools_pid=$!
  validate_wheel_hashes "${OBSERVABILITY_ARCHIVES[@]}" &
  local obs_pid=$!
  
  # Wait for all validations to complete
  local failed=0
  wait "$arch_pid" || failed=1
  wait "$tools_pid" || failed=1
  wait "$obs_pid" || failed=1
  
  if [[ $failed -ne 0 ]]; then
    echo "Wheel hash validation failed"
    exit 1
  fi
fi


#
# Install our distributions in order of dependencies
#

# First install grpcio (required by all other packages)
at_least_one_installs "${ARCHIVES[@]}"

# Install all other packages in parallel since they only depend on grpcio
install_packages_parallel


#
# Test our distributions
#

# TODO(jtattermusch): add a .proto file to the distribtest, generate python
# code from it and then use the generated code from distribtest.py
"$PYTHON" -m grpc_tools.protoc --help

"$PYTHON" distribtest.py
