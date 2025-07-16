# Copyright 2025 gRPC authors.
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
import importlib.util
import os
import pkgutil
import sys
from typing import Optional, Sequence
import unittest

from typeguard import install_import_hook

# Add all relevant grpc.aio submodules here
# Temporarily disable most hooks due to type annotation issues
# install_import_hook('grpc.aio')
# install_import_hook('grpc.aio._channel')
install_import_hook('grpc.aio._server')
install_import_hook('grpc.aio._utils')
# install_import_hook('grpc.aio._interceptor')
# install_import_hook('grpc.aio._base_channel')
# install_import_hook('grpc.aio._base_server')
# install_import_hook('grpc.aio._typing')
install_import_hook('grpc.aio._call')
# install_import_hook('grpc.aio._metadata')


class SingleLoader:
    def __init__(
        self, target_module: str, test_patterns: Optional[list[str]] = None
    ):
        loader = unittest.TestLoader()
        loader.testNamePatterns = test_patterns
        self.suite = unittest.TestSuite()
        suites = []

        # Look in the current working directory for test modules
        for _, module_name, _ in pkgutil.walk_packages([os.getcwd()]):
            if target_module in module_name:
                spec = importlib.util.find_spec(module_name)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                suites.append(loader.loadTestsFromModule(module))

        assert len(suites) == 1, f"Expected only 1 test module. Found {suites}"
        self.suite.addTest(suites[0])


def _convert_select_pattern(pattern):
    # Same as https://github.com/python/cpython/blob/v3.13.7/Lib/unittest/main.py#L50
    if not "*" in pattern:
        pattern = "*%s*" % pattern
    return pattern


def _arg_parser() -> argparse.ArgumentParser:
    # https://github.com/python/cpython/blob/v3.13.7/Lib/unittest/main.py#L161
    parser = argparse.ArgumentParser()
    parser.add_argument('-v', '--verbose', dest='verbosity',
                        action='store_const', const=2,
                        help='Verbose output')
    parser.add_argument('-q', '--quiet', dest='verbosity',
                        action='store_const', const=0,
                        help='Quiet output')
    parser.add_argument('--locals', dest='tb_locals',
                        action='store_true',
                        help='Show local variables in tracebacks')
    if sys.version_info >= (3, 12):
        parser.add_argument('--durations', dest='durations', type=int,
                            default=None, metavar="N",
                            help='Show the N slowest test cases (N=0 for all)')
    parser.add_argument('-f', '--failfast', dest='failfast',
                        action='store_true',
                        help='Stop on first fail or error')
    parser.add_argument('-k', dest='testNamePatterns',
                        action='append', type=_convert_select_pattern,
                        help='Only run tests which match the given substring')
    return parser


def main():
    if len(sys.argv) < 2:
        print(f"USAGE: {sys.argv[0]} TARGET_MODULE [TEST_ARGS]", file=sys.stderr)
        sys.exit(1)

    # Remove the current wrapper script from the args.
    sys.argv = sys.argv[1:]

    # Target module first.
    target_module = sys.argv[0]

    # Optional test args the rest.
    parsed_args = _arg_parser().parse_args(sys.argv[1:])
    test_kwargs = vars(parsed_args)

    if test_kwargs["verbosity"] is None:
        test_kwargs["verbosity"] = 2

    test_patterns = test_kwargs.pop("testNamePatterns")

    loader = SingleLoader(target_module, test_patterns=test_patterns)
    runner = unittest.TextTestRunner(**test_kwargs)

    result = runner.run(loader.suite)

    if not result.wasSuccessful():
        sys.exit("Test failure")


if __name__ == "__main__":
    main()
