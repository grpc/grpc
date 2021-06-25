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

# Expected $XDS_K8S_DRIVER_DIR to be set by the file sourcing this.
readonly XDS_K8S_DRIVER_VENV_DIR="${XDS_K8S_DRIVER_VENV_DIR:-$XDS_K8S_DRIVER_DIR/venv}"

if [[ -z "${VIRTUAL_ENV}" ]]; then
  if [[ -d "${XDS_K8S_DRIVER_VENV_DIR}" ]]; then
    # Intentional: No need to check python venv activate script.
    # shellcheck source=/dev/null
    source "${XDS_K8S_DRIVER_VENV_DIR}/bin/activate"
  else
    echo "Missing python virtual environment directory: ${XDS_K8S_DRIVER_VENV_DIR}" >&2
    echo "Follow README.md installation steps first." >&2
    exit 1
  fi
fi
