#!/bin/bash
# Copyright 2019 The gRPC authors.
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

set=-ex

VIRTUAL_ENV=bazel_format_virtual_environment

CONFIG_PATH="$(dirname ${0})/bazel_style.cfg"

python -m virtualenv ${VIRTUAL_ENV}
PYTHON=${VIRTUAL_ENV}/bin/python
"$PYTHON" -m pip install --upgrade pip==19.3.1
"$PYTHON" -m pip install yapf==0.30.0

pushd "$(dirname "${0}")/../.."
FILES=$(find . -path ./third_party -prune -o -name '*.bzl' -print)
echo "${FILES}" | xargs "$PYTHON" -m yapf -i --style="${CONFIG_PATH}"

if ! which buildifier &>/dev/null; then
    echo 'buildifer must be installed.' >/dev/stderr
    exit 1
fi

echo "${FILES}" | xargs buildifier --type=bzl

popd
