#!/bin/bash

# Shellcheck cant find the included file.
# shellcheck disable=SC1091
source common.sh

set -euxo pipefail

gcloud functions list | grep "${FUNCTION_PREFIX}" | awk '{print $1;}' |  xargs -n1 gcloud functions delete
