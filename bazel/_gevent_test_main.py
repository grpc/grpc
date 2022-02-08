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

import traceback, signal
from typing import Sequence


import grpc.experimental.gevent
grpc.experimental.gevent.init_gevent()

import gevent
import greenlet
import datetime

import grpc
import unittest
import sys
import os
import pkgutil

def trace_callback(event, args):
    if event in ("switch", "throw"):
        origin, target = args
        sys.stderr.write("{} Transfer from {} to {} with {}\n".format(datetime.datetime.now(), origin, target, event))
    else:
        sys.stderr.write("Unknown event {}.\n".format(event))
    sys.stderr.flush()

if os.getenv("GREENLET_TRACE") is not None:
    greenlet.settrace(trace_callback)

def debug(sig, frame):
    d={'_frame':frame}
    d.update(frame.f_globals)
    d.update(frame.f_locals)

    sys.stderr.write("Traceback:\n{}".format("\n".join(traceback.format_stack(frame))))
    import gevent.util; gevent.util.print_run_info()
    sys.stderr.flush()

signal.signal(signal.SIGTERM, debug)


class SingleLoader(object):
    def __init__(self, pattern: str):
        loader = unittest.TestLoader()
        self.suite = unittest.TestSuite()
        tests = []
        for importer, module_name, is_package in pkgutil.walk_packages([os.path.dirname(os.path.relpath(__file__))]):
            if pattern in module_name:
                module = importer.find_module(module_name).load_module(module_name)
                tests.append(loader.loadTestsFromModule(module))
        if len(tests) != 1:
            raise AssertionError("Expected only 1 test module. Found {}".format(tests))
        self.suite.addTest(tests[0])


    def loadTestsFromNames(self, names: Sequence[str], module: str = None) -> unittest.TestSuite:
        return self.suite

if __name__ == "__main__":

    if len(sys.argv) != 2:
        print(f"USAGE: {sys.argv[0]} TARGET_MODULE", file=sys.stderr)

    target_module = sys.argv[1]

    loader = SingleLoader(target_module)
    runner = unittest.TextTestRunner()

    result = gevent.spawn(runner.run, loader.suite)
    result.join()
    if not result.value.wasSuccessful():
        sys.exit("Test failure.")
