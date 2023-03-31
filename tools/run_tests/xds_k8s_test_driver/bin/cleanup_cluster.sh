#!/usr/bin/env bash
# Copyright 2023 gRPC authors.
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

SCRIPT_DIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
readonly SCRIPT_DIR
readonly XDS_K8S_DRIVER_DIR="${SCRIPT_DIR}/.."

cd "${XDS_K8S_DRIVER_DIR}"

mapfile -t suffixes < <(kubectl get namespaces -o json  | jq -r '.items[].metadata.name' | grep -Po '(?<=-(client|server)-)(.*)')

echo "Found suffixes: ${suffixes[*]}"

for suffix in "${suffixes[@]}"; do
  echo "-------------------- Cleaning suffix ${suffix} --------------------"
  set -x
  ./bin/cleanup.sh --nosecure "--resource_suffix=${suffix}"
  set +x
  echo "-------------------- Finished cleaning ${suffix} --------------------"
done
