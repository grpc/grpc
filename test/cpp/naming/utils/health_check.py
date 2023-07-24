#!/usr/bin/env python3
# Copyright 2023 The gRPC Authors.
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

import argparse
import platform
import subprocess
import sys
import time


def test_runner_log(msg):
    sys.stderr.write("\n%s: %s\n" % (__file__, msg))


def python_args(arg_list):
    if platform.system() == "Windows":
        return [sys.executable] + arg_list
    return arg_list


def wait_until_dns_server_is_up(args):
    for i in range(0, 30):
        test_runner_log(
            "Health check: attempt to connect to DNS server over TCP."
        )
        tcp_connect_subprocess = subprocess.Popen(
            python_args(
                [
                    args.tcp_connect_bin_path,
                    "--server_host",
                    "127.0.0.1",
                    "--server_port",
                    str(args.dns_server_port),
                    "--timeout",
                    str(1),
                ]
            )
        )
        tcp_connect_subprocess.communicate()
        if tcp_connect_subprocess.returncode == 0:
            test_runner_log(
                (
                    "Health check: attempt to make an A-record "
                    "query to DNS server."
                )
            )
            dns_resolver_subprocess = subprocess.Popen(
                python_args(
                    [
                        args.dns_resolver_bin_path,
                        "--qname",
                        "health-check-local-dns-server-is-alive.resolver-tests.grpctestingexp",
                        "--server_host",
                        "127.0.0.1",
                        "--server_port",
                        str(args.dns_server_port),
                    ]
                ),
                stdout=subprocess.PIPE,
            )
            dns_resolver_stdout, _ = dns_resolver_subprocess.communicate(
                str.encode("ascii")
            )
            if dns_resolver_subprocess.returncode == 0:
                if "123.123.123.123".encode("ascii") in dns_resolver_stdout:
                    test_runner_log(
                        (
                            "DNS server is up! "
                            "Successfully reached it over UDP and TCP."
                        )
                    )
                return
        time.sleep(1)
    test_runner_log(
        (
            "Failed to reach DNS server over TCP and/or UDP. "
            "Exitting without running tests."
        )
    )
    sys.exit(1)


def main():
    argp = argparse.ArgumentParser(description="Make DNS queries for A records")
    argp.add_argument(
        "-p",
        "--dns_server_port",
        default=None,
        type=int,
        help=("Port that local DNS server is listening on."),
    )
    argp.add_argument(
        "--dns_resolver_bin_path",
        default=None,
        type=str,
        help=("Path to the DNS health check utility."),
    )
    argp.add_argument(
        "--tcp_connect_bin_path",
        default=None,
        type=str,
        help=("Path to the TCP health check utility."),
    )
    args = argp.parse_args()
    wait_until_dns_server_is_up(args)


if __name__ == "__main__":
    main()
