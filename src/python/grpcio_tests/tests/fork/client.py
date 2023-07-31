# Copyright 2018 gRPC authors.
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
"""The Python implementation of the GRPC interoperability test client."""

import argparse
import logging
import sys

from tests.fork import methods


def _args():
    def parse_bool(value):
        if value == "true":
            return True
        if value == "false":
            return False
        raise argparse.ArgumentTypeError("Only true/false allowed")

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--server_host",
        default="localhost",
        type=str,
        help="the host to which to connect",
    )
    parser.add_argument(
        "--server_port",
        type=int,
        required=True,
        help="the port to which to connect",
    )
    parser.add_argument(
        "--test_case",
        default="large_unary",
        type=str,
        help="the test case to execute",
    )
    parser.add_argument(
        "--use_tls",
        default=False,
        type=parse_bool,
        help="require a secure connection",
    )
    return parser.parse_args()


def _test_case_from_arg(test_case_arg):
    for test_case in methods.TestCase:
        if test_case_arg == test_case.value:
            return test_case
    else:
        raise ValueError(f'No test case "{test_case_arg}"!')


def test_fork():
    logging.basicConfig(level=logging.INFO)
    args = vars(_args())
    if args["test_case"] == "all":
        for test_case in methods.TestCase:
            test_case.run_test(args)
    else:
        test_case = _test_case_from_arg(args["test_case"])
        test_case.run_test(args)


if __name__ == "__main__":
    test_fork()
