#!/usr/bin/env bash
# Copyright 2020 gRPC authors.
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
# TODO(sergiitk): move to grpc/grpc when implementing support of other languages
set -eo pipefail

# Constants
readonly PYTHON_VERSION="3.6"
# Test driver
readonly TEST_DRIVER_REPO_NAME="grpc"
readonly TEST_DRIVER_REPO_URL="https://github.com/grpc/grpc.git"
readonly TEST_DRIVER_BRANCH="${TEST_DRIVER_BRANCH:-master}"
readonly TEST_DRIVER_PATH="tools/run_tests/xds_k8s_test_driver"
readonly TEST_DRIVER_PROTOS_PATH="src/proto/grpc/testing"

#######################################
# Run command end report its exit code. Doesn't exit on non-zero exit code.
# Globals:
#   None
# Arguments:
#   Command to execute
# Outputs:
#   Writes the output of given command to stdout, stderr
#######################################
run_ignore_exit_code() {
  local exit_code=-1
  "$@" || exit_code=$?
  echo "Exit code: ${exit_code}"
}

#######################################
# Parses information about git repository at given path to global variables.
# Globals:
#   GIT_ORIGIN_URL: Populated with the origin URL of git repo used for the build
#   GIT_COMMIT: Populated with the SHA-1 of git commit being built
#   GIT_COMMIT_SHORT: Populated with the short SHA-1 of git commit being built
# Arguments:
#   Git source dir
#######################################
parse_src_repo_git_info() {
  local src_dir="${SRC_DIR:?SRC_DIR must be set}"
  readonly GIT_ORIGIN_URL=$(git -C "${src_dir}" remote get-url origin)
  readonly GIT_COMMIT=$(git -C "${src_dir}" rev-parse HEAD)
  readonly GIT_COMMIT_SHORT=$(git -C "${src_dir}" rev-parse --short HEAD)
}

#######################################
# List GCR image tags matching given tag name.
# Arguments:
#   Image name
#   Tag name
# Outputs:
#   Writes the table with the list of found tags to stdout.
#   If no tags found, the output is an empty string.
#######################################
gcloud_gcr_list_image_tags() {
  gcloud container images list-tags --format="table[box](tags,digest,timestamp.date())" --filter="tags:$2" "$1"
}

#######################################
# A helper to execute `gcloud -q components update`.
# Arguments:
#   None
# Outputs:
#   Writes the output of `gcloud` command to stdout, stderr
#######################################
gcloud_update() {
  echo "Update gcloud components:"
  gcloud -q components update
}

#######################################
# Create kube context authenticated with GKE cluster, saves context name.
# to KUBE_CONTEXT
# Globals:
#   GKE_CLUSTER_NAME
#   GKE_CLUSTER_ZONE
#   KUBE_CONTEXT: Populated with name of kubectl context with GKE cluster access
# Arguments:
#   None
# Outputs:
#   Writes the output of `gcloud` command to stdout, stderr
#   Writes authorization info $HOME/.kube/config
#######################################
gcloud_get_cluster_credentials() {
  gcloud container clusters get-credentials "${GKE_CLUSTER_NAME}" --zone "${GKE_CLUSTER_ZONE}"
  readonly KUBE_CONTEXT="$(kubectl config current-context)"
}

#######################################
# Clone the source code of the test driver to $TEST_DRIVER_REPO_DIR, unless
# given folder exists.
# Globals:
#   TEST_DRIVER_REPO_URL
#   TEST_DRIVER_BRANCH
#   TEST_DRIVER_REPO_DIR: path to the repo containing the test driver
#   TEST_DRIVER_REPO_DIR_USE_EXISTING: set non-empty value to use exiting
#      clone of the driver repo located at $TEST_DRIVER_REPO_DIR.
#      Useful for debugging the build script locally.
# Arguments:
#   None
# Outputs:
#   Writes the output of `git` command to stdout, stderr
#   Writes driver source code to $TEST_DRIVER_REPO_DIR
#######################################
test_driver_get_source() {
  if [[ -n "${TEST_DRIVER_REPO_DIR_USE_EXISTING}" && -d "${TEST_DRIVER_REPO_DIR}" ]]; then
    echo "Using exiting driver directory: ${TEST_DRIVER_REPO_DIR}."
  else
    echo "Cloning driver to ${TEST_DRIVER_REPO_URL} branch ${TEST_DRIVER_BRANCH} to ${TEST_DRIVER_REPO_DIR}"
    git clone -b "${TEST_DRIVER_BRANCH}" --depth=1 "${TEST_DRIVER_REPO_URL}" "${TEST_DRIVER_REPO_DIR}"
  fi
}

#######################################
# Install Python modules from required in $TEST_DRIVER_FULL_DIR/requirements.txt
# to Python virtual environment. Creates and activates Python venv if necessary.
# Globals:
#   TEST_DRIVER_FULL_DIR
#   PYTHON_VERSION
# Arguments:
#   None
# Outputs:
#   Writes the output of `python`, `pip` commands to stdout, stderr
#   Writes the list of installed modules to stdout
#######################################
test_driver_pip_install() {
  echo "Install python dependencies"
  cd "${TEST_DRIVER_FULL_DIR}"

  # Create and activate virtual environment unless already using one
  if [[ -z "${VIRTUAL_ENV}" ]]; then
    local venv_dir="${TEST_DRIVER_FULL_DIR}/venv"
    if [[ -d "${venv_dir}" ]]; then
      echo "Found python virtual environment directory: ${venv_dir}"
    else
      echo "Creating python virtual environment: ${venv_dir}"
      "python${PYTHON_VERSION} -m venv ${venv_dir}"
    fi
    # Intentional: No need to check python venv activate script.
    # shellcheck source=/dev/null
    source "${venv_dir}/bin/activate"
  fi

  pip install -r requirements.txt
  echo "Installed Python packages:"
  pip list
}

#######################################
# Compile proto-files needed for the test driver
# Globals:
#   TEST_DRIVER_REPO_DIR
#   TEST_DRIVER_FULL_DIR
#   TEST_DRIVER_PROTOS_PATH
# Arguments:
#   None
# Outputs:
#   Writes the output of `python -m grpc_tools.protoc` to stdout, stderr
#   Writes the list if compiled python code to stdout
#   Writes compiled python code with proto messages and grpc services to
#   $TEST_DRIVER_FULL_DIR/src/proto
#######################################
test_driver_compile_protos() {
  declare -a protos
  protos=(
    "${TEST_DRIVER_PROTOS_PATH}/test.proto"
    "${TEST_DRIVER_PROTOS_PATH}/messages.proto"
    "${TEST_DRIVER_PROTOS_PATH}/empty.proto"
  )
  echo "Generate python code from grpc.testing protos: ${protos[*]}"
  cd "${TEST_DRIVER_REPO_DIR}"
  python -m grpc_tools.protoc \
    --proto_path=. \
    --python_out="${TEST_DRIVER_FULL_DIR}" \
    --grpc_python_out="${TEST_DRIVER_FULL_DIR}" \
    "${protos[@]}"
  local protos_out_dir="${TEST_DRIVER_FULL_DIR}/${TEST_DRIVER_PROTOS_PATH}"
  echo "Generated files ${protos_out_dir}:"
  ls -Fl "${protos_out_dir}"
}

#######################################
# Installs the test driver and it's requirements.
# https://github.com/grpc/grpc/tree/master/tools/run_tests/xds_k8s_test_driver#installation
# Globals:
#   TEST_DRIVER_REPO_DIR: Populated with the path to the repo containing
#                         the test driver
#   TEST_DRIVER_FULL_DIR: Populated with the path to the test driver source code
# Arguments:
#   The directory for test driver's source code
# Outputs:
#   Writes the output to stdout, stderr
#######################################
test_driver_install() {
  readonly TEST_DRIVER_REPO_DIR="${1:?Usage test_driver_install TEST_DRIVER_REPO_DIR}"
  readonly TEST_DRIVER_FULL_DIR="${TEST_DRIVER_REPO_DIR}/${TEST_DRIVER_PATH}"
  test_driver_get_source
  test_driver_pip_install
  test_driver_compile_protos
}

#######################################
# Outputs Kokoro image version and Ubuntu's lsb_release
# Arguments:
#   None
# Outputs:
#   Writes the output to stdout
#######################################
kokoro_print_version() {
  echo "Kokoro VM version:"
  if [[ -f /VERSION ]]; then
    cat /VERSION
  fi
  run_ignore_exit_code lsb_release -a
}

#######################################
# Report extra information about the job via sponge properties.
# Globals:
#   KOKORO_ARTIFACTS_DIR
#   GIT_ORIGIN_URL
#   GIT_COMMIT_SHORT
#   TESTGRID_EXCLUDE
# Arguments:
#   None
# Outputs:
#   Writes the output to stdout
#   Writes job properties to $KOKORO_ARTIFACTS_DIR/custom_sponge_config.csv
#######################################
kokoro_write_sponge_properties() {
  # CSV format: "property_name","property_value"
  # Bump TESTS_FORMAT_VERSION when reported test name changed enough to when it
  # makes more sense to discard previous test results from a testgrid board.
  # Use GIT_ORIGIN_URL to exclude test runs executed against repo forks from
  # testgrid reports.
  cat >"${KOKORO_ARTIFACTS_DIR}/custom_sponge_config.csv" <<EOF
TESTS_FORMAT_VERSION,2
TESTGRID_EXCLUDE,${TESTGRID_EXCLUDE:-0}
GIT_ORIGIN_URL,${GIT_ORIGIN_URL:?GIT_ORIGIN_URL must be set}
GIT_COMMIT_SHORT,${GIT_COMMIT_SHORT:?GIT_COMMIT_SHORT must be set}
EOF
  echo "Sponge properties:"
  cat "${KOKORO_ARTIFACTS_DIR}/custom_sponge_config.csv"
}

#######################################
# Configure Python virtual environment on Kokoro VM.
# Arguments:
#   None
# Outputs:
#   Writes the output of `pyenv` commands to stdout
#######################################
kokoro_setup_python_virtual_environment() {
  # Kokoro provides pyenv, so use it instead of `python -m venv`
  echo "Setup pyenv environment"
  eval "$(pyenv init -)"
  eval "$(pyenv virtualenv-init -)"
  py_latest_patch="$(pyenv versions --bare --skip-aliases | grep -E "^${PYTHON_VERSION}\.[0-9]{1,2}$" | sort --version-sort | tail -n 1)"
  echo "Activating python ${py_latest_patch} virtual environment"
  pyenv virtualenv --no-pip "${py_latest_patch}" k8s_xds_test_runner
  pyenv local k8s_xds_test_runner
  pyenv activate k8s_xds_test_runner
  python -m ensurepip
  # pip is fixed to 21.0.1 due to issue https://github.com/pypa/pip/pull/9835
  # internal details: b/186411224
  # TODO(sergiitk): revert https://github.com/grpc/grpc/pull/26087 when 21.1.1 released
  python -m pip install -U pip==21.0.1
  pip --version
}

#######################################
# Installs and configures the test driver on Kokoro VM.
# Globals:
#   KOKORO_ARTIFACTS_DIR
#   TEST_DRIVER_REPO_NAME
#   SRC_DIR: Populated with absolute path to the source repo on Kokoro VM
#   TEST_DRIVER_REPO_DIR: Populated with the path to the repo containing
#                         the test driver
#   TEST_DRIVER_FULL_DIR: Populated with the path to the test driver source code
#   TEST_DRIVER_FLAGFILE: Populated with relative path to test driver flagfile
#   TEST_XML_OUTPUT_DIR: Populated with the path to test xUnit XML report
#   KUBE_CONTEXT: Populated with name of kubectl context with GKE cluster access
#   GIT_ORIGIN_URL: Populated with the origin URL of git repo used for the build
#   GIT_COMMIT: Populated with the SHA-1 of git commit being built
#   GIT_COMMIT_SHORT: Populated with the short SHA-1 of git commit being built
# Arguments:
#   The name of github repository being built
# Outputs:
#   Writes the output to stdout, stderr, files
#######################################
kokoro_setup_test_driver() {
  local src_repository_name="${1:?Usage kokoro_setup_test_driver GITHUB_REPOSITORY_NAME}"
  # Capture Kokoro VM version info in the log.
  kokoro_print_version

  # Kokoro clones repo to ${KOKORO_ARTIFACTS_DIR}/github/${GITHUB_REPOSITORY}
  local github_root="${KOKORO_ARTIFACTS_DIR}/github"
  readonly SRC_DIR="${github_root}/${src_repository_name}"
  local test_driver_repo_dir
  test_driver_repo_dir="${TEST_DRIVER_REPO_DIR:-$(mktemp -d)/${TEST_DRIVER_REPO_NAME}}"
  parse_src_repo_git_info SRC_DIR
  kokoro_write_sponge_properties
  kokoro_setup_python_virtual_environment

  # gcloud requires python, so this should be executed after pyenv setup
  gcloud_update
  gcloud_get_cluster_credentials
  test_driver_install "${test_driver_repo_dir}"
  # shellcheck disable=SC2034  # Used in the main script
  readonly TEST_DRIVER_FLAGFILE="config/grpc-testing.cfg"
  # Test artifacts dir: xml reports, logs, etc.
  local artifacts_dir="${KOKORO_ARTIFACTS_DIR}/artifacts"
  # Folders after $artifacts_dir reported as target name
  readonly TEST_XML_OUTPUT_DIR="${artifacts_dir}/${KOKORO_JOB_NAME}"
  mkdir -p "${artifacts_dir}" "${TEST_XML_OUTPUT_DIR}"
}

#######################################
# Installs and configures the test driver for testing build script locally.
# Globals:
#   TEST_DRIVER_REPO_NAME
#   TEST_DRIVER_REPO_DIR: Unless provided, populated with a temporary dir with
#                         the path to the test driver repo
#   SRC_DIR: Populated with absolute path to the source repo
#   TEST_DRIVER_FULL_DIR: Populated with the path to the test driver source code
#   TEST_DRIVER_FLAGFILE: Populated with relative path to test driver flagfile
#   TEST_XML_OUTPUT_DIR: Populated with the path to test xUnit XML report
#   GIT_ORIGIN_URL: Populated with the origin URL of git repo used for the build
#   GIT_COMMIT: Populated with the SHA-1 of git commit being built
#   GIT_COMMIT_SHORT: Populated with the short SHA-1 of git commit being built
#   KUBE_CONTEXT: Populated with name of kubectl context with GKE cluster access
# Arguments:
#   The path to the folder containing the build script
# Outputs:
#   Writes the output to stdout, stderr, files
#######################################
local_setup_test_driver() {
  local script_dir="${1:?Usage: local_setup_test_driver SCRIPT_DIR}"
  readonly SRC_DIR="$(git -C "${script_dir}" rev-parse --show-toplevel)"
  parse_src_repo_git_info SRC_DIR
  readonly KUBE_CONTEXT="${KUBE_CONTEXT:-$(kubectl config current-context)}"
  local test_driver_repo_dir
  test_driver_repo_dir="${TEST_DRIVER_REPO_DIR:-$(mktemp -d)/${TEST_DRIVER_REPO_NAME}}"
  test_driver_install "${test_driver_repo_dir}"
  # shellcheck disable=SC2034  # Used in the main script
  readonly TEST_DRIVER_FLAGFILE="config/local-dev.cfg"
  # Test out
  readonly TEST_XML_OUTPUT_DIR="${TEST_DRIVER_FULL_DIR}/out"
  mkdir -p "${TEST_XML_OUTPUT_DIR}"
}

