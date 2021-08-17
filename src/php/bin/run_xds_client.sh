#!/bin/bash
# Copyright 2021 gRPC authors.
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


# This script is being launched from the run_xds_tests.py runner and is
# responsible for managing child PHP processes.

cleanup () {
    echo "Trapped SIGTERM. Cleaning up..."
    set -x
    kill -9 $PID1
    kill -9 $PID2
    running=false
    set +x
}

trap cleanup SIGTERM

set -e
cd $(dirname $0)/../../..
root=$(pwd)

# tmp_file1 contains the list of RPCs (and their spec) the parent PHP
# process want executed
tmp_file1=$(mktemp)
# tmp_file2 contains the RPC result of each key initiated
tmp_file2=$(mktemp)

set -x
# This is the PHP parent process, who is primarily responding to the
# run_xds_tests.py runner's stats requests
php -d extension=grpc.so -d extension=pthreads.so \
    src/php/tests/interop/xds_client.php $1 $2 $3 $4 $5 $6 \
    --tmp_file1=$tmp_file1 --tmp_file2=$tmp_file2 &
PID1=$!

# This script watches RPCs written to tmp_file1, spawn off more PHP
# child processes to execute them, and writes the result to tmp_file2
python3 -u src/php/bin/xds_manager.py \
        --tmp_file1=$tmp_file1 --tmp_file2=$tmp_file2 \
        --bootstrap_path=$GRPC_XDS_BOOTSTRAP &
PID2=$!
set +x

# This will be killed by a SIGTERM signal from the run_xds_tests.py
# runner
running=true
while $running
do
    sleep 1
done
