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

XDS_K8S_DRIVER_DIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
readonly XDS_K8S_DRIVER_DIR
readonly XDS_K8S_CONFIG="${XDS_K8S_CONFIG:-$XDS_K8S_DRIVER_DIR/config/local-dev.cfg}"

display_usage() {
  cat <<EOF >/dev/stderr
Convenience script to execute tests/ and helper bin/ scripts.

USAGE: $0 script_path [arguments]
   script_path: path to python script to execute, relative to driver root folder
   arguments ...: arguments passed to program in sys.argv

ENVIRONMENT:
   XDS_K8S_CONFIG: file path to the config flagfile, relative to
                   driver root folder. Default: config/local-dev.cfg
                   Will be appended as --flagfile="config_absolute_path" argument
   XDS_K8S_DRIVER_VENV_DIR: the path to python virtual environment directory
                            Default: $XDS_K8S_DRIVER_DIR/venv
DESCRIPTION:
This tool performs the following:
1) Ensures python virtual env installed and activated
2) Exports test driver root in PYTHONPATH
3) Automatically appends --flagfile="\$XDS_K8S_CONFIG" argument

EXAMPLES:
$0 bin/run_td_setup.py --help      # list script-specific options
$0 bin/run_td_setup.py --helpfull  # list all available options
XDS_K8S_CONFIG=./path-to-flagfile.cfg ./run.sh bin/run_td_setup.py --resource_suffix=override-suffix
$0 tests/baseline_test.py
$0 tests/security_test.py --verbosity=1 --logger_levels=__main__:DEBUG,framework:DEBUG
$0 tests/security_test.py SecurityTest.test_mtls --nocheck_local_certs
EOF
  exit 1
}

if [[ "$#" -eq 0 || "$1" = "-h" || "$1" = "--help" ]]; then
  display_usage
fi

# Relative paths not yet supported by shellcheck.
# shellcheck source=/dev/null
source "${XDS_K8S_DRIVER_DIR}/bin/ensure_venv.sh"

cd "${XDS_K8S_DRIVER_DIR}"
export PYTHONPATH="${XDS_K8S_DRIVER_DIR}"
# Split path to python file from the rest of the args.
readonly PY_FILE="$1"
shift

if [[ "${PY_FILE}" =~ tests/unit($|/) ]]; then
  # Do not set the flagfile when running unit tests.
  exec python "${PY_FILE}" "$@"
else
  # Append args after --flagfile, so they take higher priority.
  exec python "${PY_FILE}" --flagfile="${XDS_K8S_CONFIG}" "$@"
fi
