#!/usr/bin/env python
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
"""Manage PHP child processes for the main PHP xDS Interop client"""

import argparse
import fcntl
import os
import subprocess

# This script is being launched from src/php/bin/run_xds_client.sh
# to manage PHP child processes which will send 1 RPC each
# asynchronously. This script keeps track of all those open
# processes and reports back to the main PHP interop client each
# of the child RPCs' status code.

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tmp_file1", nargs="?", default="")
    parser.add_argument("--tmp_file2", nargs="?", default="")
    parser.add_argument("--bootstrap_path", nargs="?", default="")
    args = parser.parse_args()
    server_address = ""
    rpcs_started = []
    open_processes = {}
    client_env = dict(os.environ)
    client_env["GRPC_XDS_BOOTSTRAP"] = args.bootstrap_path
    while True:
        # tmp_file1 contains a list of RPCs (and their spec) the parent process
        # wants executed
        f1 = open(args.tmp_file1, "r+")
        fcntl.flock(f1, fcntl.LOCK_EX)
        while True:
            key = f1.readline()
            if not key:
                break
            key = key.strip()
            if key.startswith("server_address"):
                if not server_address:
                    server_address = key[15:]
            elif not key in rpcs_started:
                # format here needs to be in sync with
                # src/php/tests/interop/xds_client.php
                items = key.split("|")
                num = items[0]
                metadata = items[2]
                timeout_sec = items[3]
                if items[1] == "UnaryCall":
                    p = subprocess.Popen(
                        [
                            "php",
                            "-d",
                            "extension=grpc.so",
                            "-d",
                            "extension=pthreads.so",
                            "src/php/tests/interop/xds_unary_call.php",
                            "--server=" + server_address,
                            "--num=" + str(num),
                            "--metadata=" + metadata,
                            "--timeout_sec=" + timeout_sec,
                        ],
                        env=client_env,
                    )
                elif items[1] == "EmptyCall":
                    p = subprocess.Popen(
                        [
                            "php",
                            "-d",
                            "extension=grpc.so",
                            "-d",
                            "extension=pthreads.so",
                            "src/php/tests/interop/xds_empty_call.php",
                            "--server=" + server_address,
                            "--num=" + str(num),
                            "--metadata=" + metadata,
                            "--timeout_sec=" + timeout_sec,
                        ],
                        env=client_env,
                    )
                else:
                    continue
                rpcs_started.append(key)
                open_processes[key] = p
        f1.truncate(0)
        fcntl.flock(f1, fcntl.LOCK_UN)
        f1.close()
        # tmp_file2 contains the RPC result of each key received from tmp_file1
        f2 = open(args.tmp_file2, "a")
        fcntl.flock(f2, fcntl.LOCK_EX)
        keys_to_delete = []
        for key, process in open_processes.items():
            result = process.poll()
            if result is not None:
                # format here needs to be in sync with
                # src/php/tests/interop/xds_client.php
                f2.write(key + "," + str(process.returncode) + "\n")
                keys_to_delete.append(key)
        for key in keys_to_delete:
            del open_processes[key]
        fcntl.flock(f2, fcntl.LOCK_UN)
        f2.close()
