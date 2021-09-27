# Copyright 2020 The gRPC Authors
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

import grpc
import unittest
import sys
import os
import pkgutil

import zope
from zope.event import subscribers

class SingleLoader(object):
    def __init__(self, pattern):
        self._pattern = pattern
        self.suite = unittest.TestSuite()
        self._loader = unittest.TestLoader()
        tests = []
        for importer, module_name, is_package in pkgutil.walk_packages([os.path.dirname(os.path.relpath(__file__))]):
            if self._pattern in module_name:
                module = importer.find_module(module_name).load_module(module_name)
                tests.append(self._loader.loadTestsFromModule(module))
        if len(tests) != 1:
            raise AssertionError("Expected only 1 test module. Found {}".format(tests))
        self.suite.addTest(tests[0])


    def loadTestsFromNames(self, names, module=None):
        return self.suite

from gevent import monkey

monkey.patch_all()

import grpc.experimental.gevent
grpc.experimental.gevent.init_gevent()
import gevent

loader = SingleLoader(sys.argv[1])
runner = unittest.TextTestRunner()

result = gevent.spawn(runner.run, loader.suite)
result.join()
if not result.value.wasSuccessful():
    sys.exit("Test failure.")
