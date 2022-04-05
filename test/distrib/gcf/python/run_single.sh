#!/bin/bash
# Copyright 2022 gRPC authors.
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

cd "$(dirname "$0")"

# Shellcheck cant find the included file.
# shellcheck disable=SC1091
source common.sh

set -euxo pipefail

RUNTIME="$1"
ARTIFACT_URL="$2"

REQUEST_COUNT=20
LOG_QUIESCE_SECONDS=10

rm -f requirements.txt
cp requirements.txt.base requirements.txt
echo "${ARTIFACT_URL}" >>requirements.txt

# Generate Function name.
FUNCTION_NAME="${FUNCTION_PREFIX}-$(uuidgen)"

function cleanup() {
  # Wait for logs to quiesce.
  sleep "${LOG_QUIESCE_SECONDS}"
  gcloud functions logs read "${FUNCTION_NAME}" || true
  (yes || true) | gcloud functions delete "${FUNCTION_NAME}"
}

trap cleanup SIGINT SIGTERM EXIT

# Deploy
DEPLOY_OUTPUT=$(gcloud functions deploy "${FUNCTION_NAME}" --entry-point test_publish --runtime "${RUNTIME}" --trigger-http --allow-unauthenticated)
HTTP_URL=$(echo "${DEPLOY_OUTPUT}" | grep "url: " | awk '{print $2;}')

# Send Requests
for _ in $(seq 1 "${REQUEST_COUNT}"); do
  GCP_IDENTITY_TOKEN=$(gcloud auth print-identity-token 2>/dev/null);
  curl -v -H "Authorization: Bearer $GCP_IDENTITY_TOKEN" "${HTTP_URL}"
done
