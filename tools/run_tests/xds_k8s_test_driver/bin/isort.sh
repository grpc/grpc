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
A helper to run isort import sorter.

USAGE: $0 [--diff]
   --diff: Do not apply changes, only show the diff

ENVIRONMENT:
   XDS_K8S_DRIVER_VENV_DIR: the path to python virtual environment directory
                            Default: $XDS_K8S_DRIVER_DIR/venv
EXAMPLES:
$0
$0 --diff
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

# Relative paths not yet supported by shellcheck.
# shellcheck source=/dev/null
source "${XDS_K8S_DRIVER_DIR}/bin/ensure_venv.sh"

if [[ "$1" == "--diff" ]]; then
  readonly MODE="--diff"
else
  readonly MODE="--overwrite-in-place"
fi

# typing is the only module allowed to put imports on the same line:
# https://google.github.io/styleguide/pyguide.html#313-imports-formatting
exec python -m isort "${MODE}" \
  --settings-path=../../../black.toml \
  framework bin tests

