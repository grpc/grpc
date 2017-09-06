#!/bin/bash
# Copyright 2015 gRPC authors.
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

# change to root directory
cd "$(dirname "${0}")/../.."

DIRS=(
    'src/python'
)
EXCLUSIONS=(
    'grpcio/grpc_*.py'
    'grpcio_health_checking/grpc_*.py'
    'grpcio_reflection/grpc_*.py'
    'grpcio_testing/grpc_*.py'
    'grpcio_tests/grpc_*.py'
)

VIRTUALENV=yapf_virtual_environment

virtualenv $VIRTUALENV
PYTHON=$(realpath "${VIRTUALENV}/bin/python")
$PYTHON -m pip install --upgrade pip
$PYTHON -m pip install --upgrade futures
$PYTHON -m pip install yapf==0.16.0

yapf() {
    local exclusion exclusion_args=()
    for exclusion in "${EXCLUSIONS[@]}"; do
        exclusion_args+=( "--exclude" "$1/${exclusion}" )
    done
    $PYTHON -m yapf -i -r --style=setup.cfg -p "${exclusion_args[@]}" "${1}"
}

if [[ -z "${TEST}" ]]; then
    for dir in "${DIRS[@]}"; do
	yapf "${dir}"
    done
else
    ok=yes
    for dir in "${DIRS[@]}"; do
	tempdir=$(mktemp -d)
	cp -RT "${dir}" "${tempdir}"
	yapf "${tempdir}"
	diff -ru "${dir}" "${tempdir}" || ok=no
	rm -rf "${tempdir}"
    done
    if [[ ${ok} == no ]]; then
	false
    fi
fi
