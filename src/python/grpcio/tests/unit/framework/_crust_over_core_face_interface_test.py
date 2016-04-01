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

"""Tests Face interface compliance of the crust-over-core stack."""

import collections
import unittest

import six

from grpc.framework.core import implementations as core_implementations
from grpc.framework.crust import implementations as crust_implementations
from grpc.framework.foundation import logging_pool
from grpc.framework.interfaces.links import utilities
from tests.unit.framework.common import test_constants
from tests.unit.framework.interfaces.face import test_cases
from tests.unit.framework.interfaces.face import test_interfaces
from tests.unit.framework.interfaces.links import test_utilities


class _Implementation(test_interfaces.Implementation):

  def instantiate(
      self, methods, method_implementations, multi_method_implementation):
    pool = logging_pool.pool(test_constants.POOL_SIZE)
    servicer = crust_implementations.servicer(
        method_implementations, multi_method_implementation, pool)

    service_end_link = core_implementations.service_end_link(
        servicer, test_constants.DEFAULT_TIMEOUT,
        test_constants.MAXIMUM_TIMEOUT)
    invocation_end_link = core_implementations.invocation_end_link()
    invocation_end_link.join_link(service_end_link)
    service_end_link.join_link(invocation_end_link)
    service_end_link.start()
    invocation_end_link.start()

    generic_stub = crust_implementations.generic_stub(invocation_end_link, pool)
    # TODO(nathaniel): Add a "groups" attribute to _digest.TestServiceDigest.
    group = next(iter(methods))[0]
    # TODO(nathaniel): Add a "cardinalities_by_group" attribute to
    # _digest.TestServiceDigest.
    cardinalities = {
        method: method_object.cardinality()
        for (group, method), method_object in six.iteritems(methods)}
    dynamic_stub = crust_implementations.dynamic_stub(
        invocation_end_link, group, cardinalities, pool)

    return generic_stub, {group: dynamic_stub}, (
        invocation_end_link, service_end_link, pool)

  def destantiate(self, memo):
    invocation_end_link, service_end_link, pool = memo
    invocation_end_link.stop(0).wait()
    service_end_link.stop(0).wait()
    invocation_end_link.join_link(utilities.NULL_LINK)
    service_end_link.join_link(utilities.NULL_LINK)
    pool.shutdown(wait=True)

  def invocation_metadata(self):
    return object()

  def initial_metadata(self):
    return object()

  def terminal_metadata(self):
    return object()

  def code(self):
    return object()

  def details(self):
    return object()

  def metadata_transmitted(self, original_metadata, transmitted_metadata):
    return original_metadata is transmitted_metadata


def load_tests(loader, tests, pattern):
  return unittest.TestSuite(
      tests=tuple(
          loader.loadTestsFromTestCase(test_case_class)
          for test_case_class in test_cases.test_cases(_Implementation())))


if __name__ == '__main__':
  unittest.main(verbosity=2)
