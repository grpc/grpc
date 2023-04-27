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

NO_SECURE="yes"
DATE_TO=$(date -Iseconds)

while [[ $# -gt 0 ]]; do
  case $1 in
    --secure) NO_SECURE=""; shift ;;
    --date_to=*) DATE_TO="${1#*=}T00:00:00Z"; shift ;;
    *) echo "Unknown argument $1"; exit 1 ;;
  esac
done

jq_selector=$(cat <<- 'EOM'
  .items[].metadata |
  select(
    (.name | test("-(client|server)-")) and
    (.creationTimestamp < $date_to)
  ) | .name
EOM
)

mapfile -t namespaces < <(\
  kubectl get namespaces --sort-by='{.metadata.creationTimestamp}'\
                         --selector='owner=xds-k8s-interop-test'\
                          -o json\
  | jq --arg date_to "${DATE_TO}" -r "${jq_selector}"
)
  
if [[ -z "${namespaces[*]}"  ]]; then
    echo "All clean."
    exit 0
fi

echo "Found namespaces:"
namespaces_joined=$(IFS=,; printf '%s' "${namespaces[*]}")
kubectl get namespaces --sort-by='{.metadata.creationTimestamp}' \
                       --selector="name in (${namespaces_joined})"

# Suffixes
mapfile -t suffixes < <(\
  printf '%s\n' "${namespaces[@]}" | sed -E 's/^.+-(server|client)-//'
)
echo
echo "Found suffixes: ${suffixes[*]}"
echo "Count: ${#namespaces[@]}"

echo "Run plan:"
for suffix in "${suffixes[@]}"; do
  echo ./bin/cleanup.sh ${NO_SECURE:+"--nosecure"} "--resource_suffix=${suffix}"
done

read -r -n 1 -p "Continue? (y/N) " answer
if [[ "$answer" != "${answer#[Yy]}" ]] ;then
  echo
  echo "Starting the cleanup."
else
  echo
  echo "Exit"
  exit 0
fi

failed=0
for suffix in "${suffixes[@]}"; do
  echo "-------------------- Cleaning suffix ${suffix} --------------------"
  set -x
  ./bin/cleanup.sh ${NO_SECURE:+"--nosecure"} "--resource_suffix=${suffix}" || (( ++failed ))
  set +x
  echo "-------------------- Finished cleaning ${suffix} --------------------"
done
echo "Failed runs: ${failed}"
if (( failed > 0 )); then
  exit 1
fi
