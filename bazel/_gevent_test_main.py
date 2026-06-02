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
import importlib

def _patch_grpc_localhost():
    import grpc
    
    def patch_address(address):
        if not isinstance(address, str):
            return address
        if address.startswith("[::]:"):
            return address.replace("[::]:", "127.0.0.1:", 1)
        if address.startswith("localhost:"):
            return address.replace("localhost:", "127.0.0.1:", 1)
        if address == "[::]":
            return "127.0.0.1"
        if address == "localhost":
            return "127.0.0.1"
        return address

    orig_server = grpc.server
    def new_server(*args, **kwargs):
        server = orig_server(*args, **kwargs)
        orig_add_insecure = server.add_insecure_port
        def new_add_insecure(address):
            return orig_add_insecure(patch_address(address))
        server.add_insecure_port = new_add_insecure
        
        orig_add_secure = server.add_secure_port
        def new_add_secure(address, *args, **kwargs):
            return orig_add_secure(patch_address(address), *args, **kwargs)
        server.add_secure_port = new_add_secure
        return server
    grpc.server = new_server

    orig_insecure_channel = grpc.insecure_channel
    def new_insecure_channel(target, *args, **kwargs):
        return orig_insecure_channel(patch_address(target), *args, **kwargs)
    grpc.insecure_channel = new_insecure_channel

    orig_secure_channel = grpc.secure_channel
    def new_secure_channel(target, *args, **kwargs):
        return orig_secure_channel(patch_address(target), *args, **kwargs)
    grpc.secure_channel = new_secure_channel

    try:
        from grpc.experimental import aio
        orig_aio_server = aio.server
        def new_aio_server(*args, **kwargs):
            server = orig_aio_server(*args, **kwargs)
            orig_add_insecure = server.add_insecure_port
            def new_add_insecure(address):
                return orig_add_insecure(patch_address(address))
            server.add_insecure_port = new_add_insecure
            
            orig_add_secure = server.add_secure_port
            def new_add_secure(address, *args, **kwargs):
                return orig_add_secure(patch_address(address), *args, **kwargs)
            server.add_secure_port = new_add_secure
            return server
        aio.server = new_aio_server

        orig_aio_insecure_channel = aio.insecure_channel
        def new_aio_insecure_channel(target, *args, **kwargs):
            return orig_aio_insecure_channel(patch_address(target), *args, **kwargs)
        aio.insecure_channel = new_aio_insecure_channel

        orig_aio_secure_channel = aio.secure_channel
        def new_aio_secure_channel(target, *args, **kwargs):
            return orig_aio_secure_channel(patch_address(target), *args, **kwargs)
        aio.secure_channel = new_aio_secure_channel
    except (ImportError, AttributeError):
        pass

if sys.platform == "darwin":
    _patch_grpc_localhost()

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


class SingleLoader:
    def __init__(self, pattern: str):
        loader = unittest.TestLoader()
        self.suite = unittest.TestSuite()
        tests = []
        for importer, module_name, is_package in pkgutil.walk_packages([os.path.dirname(os.path.relpath(__file__))]):
            if pattern in module_name:
                spec = importer.find_spec(module_name)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
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
