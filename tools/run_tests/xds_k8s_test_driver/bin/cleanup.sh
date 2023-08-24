#!/usr/bin/env bash
# Copyright 2021 gRPC authors.
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

set -eo pipefail

display_usage() {
  cat <<EOF >/dev/stderr
Performs full TD and K8S resource cleanup

USAGE: $0 [--nosecure] [arguments]
   --nosecure: Skip cleanup for the resources specific for PSM Security
   arguments ...: additional arguments passed to ./run.sh

ENVIRONMENT:
   XDS_K8S_CONFIG: file path to the config flagfile, relative to
                   driver root folder. Default: config/local-dev.cfg
                   Will be appended as --flagfile="config_absolute_path" argument
   XDS_K8S_DRIVER_VENV_DIR: the path to python virtual environment directory
                            Default: $XDS_K8S_DRIVER_DIR/venv
EXAMPLES:
$0
$0 --nosecure
XDS_K8S_CONFIG=./path-to-flagfile.cfg $0 --resource_suffix=override-suffix
EOF
  exit 1
}

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
  display_usage
fi

SCRIPT_DIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
readonly SCRIPT_DIR
readonly XDS_K8S_DRIVER_DIR="${SCRIPT_DIR}/.."

cd "${XDS_K8S_DRIVER_DIR}"

if [[ "$1" == "--nosecure" ]]; then
  shift
  ./run.sh bin/run_td_setup.py --cmd=cleanup "$@" && \
  ./run.sh bin/run_test_client.py --cmd=cleanup --cleanup_namespace "$@" && \
  ./run.sh bin/run_test_server.py --cmd=cleanup --cleanup_namespace "$@"
else
  ./run.sh bin/run_td_setup.py --cmd=cleanup --security=mtls "$@" && \
  ./run.sh bin/run_test_client.py --cmd=cleanup --cleanup_namespace --mode=secure "$@" && \
  ./run.sh bin/run_test_server.py --cmd=cleanup --cleanup_namespace --mode=secure "$@"
fi
