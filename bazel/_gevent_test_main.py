# Copyright 2021 The gRPC Authors
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

import gevent
from gevent import monkey

monkey.patch_all()
threadpool = gevent.hub.get_hub().threadpool

# Currently, each channel corresponds to a single native thread in the
# gevent threadpool. Thus, when the unit test suite spins up hundreds of
# channels concurrently, some will be starved out, causing the test to
# increase in duration. We increase the max size here so this does not
# happen.
threadpool.maxsize = 1024
threadpool.size = 32

import datetime
import os
import signal
import sys
import traceback
import unittest

import greenlet
import grpc
import grpc.experimental.gevent

try:
    from bazel._single_loader import SingleLoader
except ImportError:
    from _single_loader import SingleLoader

grpc.experimental.gevent.init_gevent()

if __name__ == "__main__":

    if len(sys.argv) < 3:
        print(
            f"USAGE: {sys.argv[0]} TARGET_MODULE UNITTEST_PATH", file=sys.stderr
        )
        sys.exit(1)

    target_module = sys.argv[1]
    unittest_path = sys.argv[2]

    loader = SingleLoader(target_module, unittest_path)
    runner = unittest.TextTestRunner()

    result = gevent.spawn(runner.run, loader.suite)
    result.join()
    if not result.value.wasSuccessful():
        sys.exit("Test failure.")
