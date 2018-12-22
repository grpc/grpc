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

# change to grpc repo root
cd "$(dirname "$0")/../../.."

PYTHON=$(realpath "${1:-py27/bin/python}")

ROOT=$(pwd)

#(export GRPC_PYTHON_TESTRUNNER_FILTER=unit._channel_ready_future_test && $PYTHON "$ROOT/src/python/grpcio_tests/setup.py" "$2" || true)

if $PYTHON "$ROOT/src/python/grpcio_tests/setup.py" "$2"; then
    echo passed
else
    # Ubunut sends crash reports through apport
    ls "$ROOT"
    COREFILE=core #$(find "$ROOT" -name "core*" | head -n 1) # find core file
    if [[ -f "$COREFILE" ]]; then gdb python "$COREFILE" example -ex "thread apply all bt" -ex "set pagination 0" -batch; fi
    exit 1
    # ls "$ROOT/src/python/grpcio_tests"
    # COREFILE=$(find "$ROOT/src/python/grpcio_tests" -maxdepth 1 -name "core*" | head -n 1) # find core file
    # if [[ -f "$COREFILE" ]]; then gdb python "$COREFILE" example -ex "thread apply all bt" -ex "set pagination 0" -batch; fi
    # exit 1
    # ls /var/crash/
    # cat /var/crash/*
    # exit 1
fi

mkdir -p "$ROOT/reports"
rm -rf "$ROOT/reports/python-coverage"
(mv -T "$ROOT/htmlcov" "$ROOT/reports/python-coverage") || true

