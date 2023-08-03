#!/bin/bash
# Copyright 2023 The gRPC Authors
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

set -ex

ARCHIVE_WITH_SUBMODULES="$1"
OUTFILE="$2"
BUILD_SCRIPT="$3"
shift 3

# Extract grpc repo archive
tar -xopf ${ARCHIVE_WITH_SUBMODULES}
cd grpc

mkdir -p artifacts

../"${BUILD_SCRIPT}" "$@" || FAILED="true"

# TODO: deterministic tar
tar -czvf ../"${OUTFILE}" artifacts

if [ "$FAILED" != "" ]
then
  exit 1
fi

