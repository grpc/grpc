#!/bin/bash
# Copyright 2016 gRPC authors.
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

cd "$(dirname "$0")/../../.."

mkdir -p artifacts/

# All the python packages have been built in the artifact phase already
# and we only collect them here to deliver them to the distribtest phase.
cp -r "${EXTERNAL_GIT_ROOT}"/input_artifacts/python_*/* artifacts/ || true

export PYTHON=${PYTHON:-python}

strip_binary_wheel() {
    WHEEL_PATH="$1"
    TEMP_WHEEL_DIR=$(mktemp -d)
    ${PYTHON} -m wheel unpack "$WHEEL_PATH" -d "$TEMP_WHEEL_DIR"
    find "$TEMP_WHEEL_DIR" -name "_protoc_compiler*.so" -exec strip --strip-debug {} ";"
    find "$TEMP_WHEEL_DIR" -name "cygrpc*.so" -exec strip --strip-debug {} ";"

    WHEEL_FILE=$(basename "$WHEEL_PATH")
    DISTRIBUTION_NAME=$(basename "$WHEEL_PATH" | cut -d '-' -f 1)
    VERSION=$(basename "$WHEEL_PATH" | cut -d '-' -f 2)
    ${PYTHON} -m wheel pack "$TEMP_WHEEL_DIR/$DISTRIBUTION_NAME-$VERSION" -d "$TEMP_WHEEL_DIR"
    mv "$TEMP_WHEEL_DIR/$WHEEL_FILE" "$WHEEL_PATH"
}

for wheel in artifacts/*.whl; do
    strip_binary_wheel "$wheel"
done

# TODO: all the artifact builder configurations generate a grpcio-VERSION.tar.gz
# source distribution package, and only one of them will end up
# in the artifacts/ directory. They should be all equivalent though.
