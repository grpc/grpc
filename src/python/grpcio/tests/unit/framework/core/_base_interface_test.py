# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Tests the RPC Framework Core's implementation of the Base interface."""

import logging
import random
import time
import unittest

from grpc.framework.core import implementations
from grpc.framework.interfaces.base import utilities
from tests.unit.framework.common import test_constants
from tests.unit.framework.interfaces.base import test_cases
from tests.unit.framework.interfaces.base import test_interfaces


class _Implementation(test_interfaces.Implementation):

  def __init__(self):
    self._invocation_initial_metadata = object()
    self._service_initial_metadata = object()
    self._invocation_terminal_metadata = object()
    self._service_terminal_metadata = object()

  def instantiate(self, serializations, servicer):
    invocation = implementations.invocation_end_link()
    service = implementations.service_end_link(
        servicer, test_constants.DEFAULT_TIMEOUT,
        test_constants.MAXIMUM_TIMEOUT)
    invocation.join_link(service)
    service.join_link(invocation)
    return invocation, service, None

  def destantiate(self, memo):
    pass

  def invocation_initial_metadata(self):
    return self._invocation_initial_metadata

  def service_initial_metadata(self):
    return self._service_initial_metadata

  def invocation_completion(self):
    return utilities.completion(self._invocation_terminal_metadata, None, None)

  def service_completion(self):
    return utilities.completion(self._service_terminal_metadata, None, None)

  def metadata_transmitted(self, original_metadata, transmitted_metadata):
    return transmitted_metadata is original_metadata

  def completion_transmitted(self, original_completion, transmitted_completion):
    return (
        (original_completion.terminal_metadata is
         transmitted_completion.terminal_metadata) and
        original_completion.code is transmitted_completion.code and
        original_completion.message is transmitted_completion.message
    )


def load_tests(loader, tests, pattern):
  return unittest.TestSuite(
      tests=tuple(
          loader.loadTestsFromTestCase(test_case_class)
          for test_case_class in test_cases.test_cases(_Implementation())))


if __name__ == '__main__':
  unittest.main(verbosity=2)
