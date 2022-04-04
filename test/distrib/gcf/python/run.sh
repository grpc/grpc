#!/bin/bash

set -euxo pipefail

cd $(dirname $0)

ARTIFACT_DIRECTORY="$1"

BUCKET_NAME="grpc-gcf-distribtests"

RUN_ID=$(uuidgen)
BASEDIR=$(dirname "$0")

FAILED_RUNTIMES=""

# This is the only programmatic way to get access to the list of runtimes.
# While hacky, it's better than the alternative -- manually upgrading a
# hand-curated list every few months.
RUNTIMES=$(gcloud functions deploy --help | egrep -o "python[0-9]+" | sort | uniq)

while read -r RUNTIME; do
  BARE_VERSION=$(echo "${RUNTIME}" | sed 's/python//g')

  # We sort to get the latest manylinux version.
  ARTIFACT=$(find ${ARTIFACT_DIRECTORY} -regex '.*grpcio-[0-9\.]+.+-cp'"${BARE_VERSION}"'-cp'"${BARE_VERSION}"'m?-manylinux.+x86_64\.whl' | sort -r | head -n 1)
  ARTIFACT_BASENAME=$(basename "${ARTIFACT}")

  # Upload artifact to GCS so GCF can access it.
  # A 1 day retention policy is active on this bucket.
  gsutil cp "${ARTIFACT}" "gs://${BUCKET_NAME}/${RUN_ID}/${ARTIFACT_BASENAME}"

  echo "Testing runtime ${RUNTIME} with artifact ${ARTIFACT_BASENAME}"
  $BASEDIR/run_single.sh "${RUNTIME}" "https://storage.googleapis.com/${BUCKET_NAME}/${RUN_ID}/${ARTIFACT_BASENAME}" || FAILED_RUNTIMES="${FAILED_RUNTIMES} ${RUNTIME}"
done<<<"${RUNTIMES}"

if [ "$FAILED_RUNTIMES" != "" ]
then
  echo "GCF Distribtest failed: Failing runtimes: ${FAILED_RUNTIMES}"
  exit 1
fi
