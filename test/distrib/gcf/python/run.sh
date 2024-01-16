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

set -euxo pipefail

cd "$(dirname "$0")"

ARTIFACT_DIRECTORY="$1"

BUCKET_NAME="grpc-gcf-distribtests"

RUN_ID=$(uuidgen)

FAILED_RUNTIMES=""

RUNTIMES=$(gcloud functions runtimes list --filter name:python* --region us-west1 | grep python | awk '{print $1}')

while read -r RUNTIME; do
  BARE_VERSION=${RUNTIME//python/}

  # We sort to get the latest manylinux version.
  ARTIFACT=$(find "${ARTIFACT_DIRECTORY}" -regex '.*grpcio-[0-9\.]+.+-cp'"${BARE_VERSION}"'-cp'"${BARE_VERSION}"'m?-manylinux.+x86_64\.whl' | sort -r | head -n 1)
  ARTIFACT_BASENAME=$(basename "${ARTIFACT}")

  # Upload artifact to GCS so GCF can access it.
  # A 1 day retention policy is active on this bucket.
  gsutil cp "${ARTIFACT}" "gs://${BUCKET_NAME}/${RUN_ID}/${ARTIFACT_BASENAME}"

  echo "Testing runtime ${RUNTIME} with artifact ${ARTIFACT_BASENAME}"
  ./run_single.sh "${RUNTIME}" "https://storage.googleapis.com/${BUCKET_NAME}/${RUN_ID}/${ARTIFACT_BASENAME}" || FAILED_RUNTIMES="${FAILED_RUNTIMES} ${RUNTIME}"
done<<<"${RUNTIMES}"

if [ "$FAILED_RUNTIMES" != "" ]
then
  echo "GCF Distribtest failed: Failing runtimes: ${FAILED_RUNTIMES}"
  exit 1
fi
