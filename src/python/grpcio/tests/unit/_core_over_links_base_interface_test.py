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

"""Tests Base interface compliance of the core-over-gRPC-links stack."""

import collections
import logging
import random
import time
import unittest

import six

from grpc._adapter import _intermediary_low
from grpc._links import invocation
from grpc._links import service
from grpc.beta import interfaces as beta_interfaces
from grpc.framework.core import implementations
from grpc.framework.interfaces.base import utilities
from tests.unit import test_common as grpc_test_common
from tests.unit.framework.common import test_constants
from tests.unit.framework.interfaces.base import test_cases
from tests.unit.framework.interfaces.base import test_interfaces


class _SerializationBehaviors(
    collections.namedtuple(
        '_SerializationBehaviors',
        ('request_serializers', 'request_deserializers', 'response_serializers',
         'response_deserializers',))):
  pass


class _Links(
    collections.namedtuple(
        '_Links',
        ('invocation_end_link', 'invocation_grpc_link', 'service_grpc_link',
         'service_end_link'))):
  pass


def _serialization_behaviors_from_serializations(serializations):
  request_serializers = {}
  request_deserializers = {}
  response_serializers = {}
  response_deserializers = {}
  for (group, method), serialization in six.iteritems(serializations):
    request_serializers[group, method] = serialization.serialize_request
    request_deserializers[group, method] = serialization.deserialize_request
    response_serializers[group, method] = serialization.serialize_response
    response_deserializers[group, method] = serialization.deserialize_response
  return _SerializationBehaviors(
      request_serializers, request_deserializers, response_serializers,
      response_deserializers)


class _Implementation(test_interfaces.Implementation):

  def instantiate(self, serializations, servicer):
    serialization_behaviors = _serialization_behaviors_from_serializations(
        serializations)
    invocation_end_link = implementations.invocation_end_link()
    service_end_link = implementations.service_end_link(
        servicer, test_constants.DEFAULT_TIMEOUT,
        test_constants.MAXIMUM_TIMEOUT)
    service_grpc_link = service.service_link(
        serialization_behaviors.request_deserializers,
        serialization_behaviors.response_serializers)
    port = service_grpc_link.add_port('[::]:0', None)
    channel = _intermediary_low.Channel('localhost:%d' % port, None)
    invocation_grpc_link = invocation.invocation_link(
        channel, b'localhost', None,
        serialization_behaviors.request_serializers,
        serialization_behaviors.response_deserializers)

    invocation_end_link.join_link(invocation_grpc_link)
    invocation_grpc_link.join_link(invocation_end_link)
    service_end_link.join_link(service_grpc_link)
    service_grpc_link.join_link(service_end_link)
    invocation_grpc_link.start()
    service_grpc_link.start()
    return invocation_end_link, service_end_link, (
        invocation_grpc_link, service_grpc_link)

  def destantiate(self, memo):
    invocation_grpc_link, service_grpc_link = memo
    invocation_grpc_link.stop()
    service_grpc_link.begin_stop()
    service_grpc_link.end_stop()

  def invocation_initial_metadata(self):
    return grpc_test_common.INVOCATION_INITIAL_METADATA

  def service_initial_metadata(self):
    return grpc_test_common.SERVICE_INITIAL_METADATA

  def invocation_completion(self):
    return utilities.completion(None, None, None)

  def service_completion(self):
    return utilities.completion(
        grpc_test_common.SERVICE_TERMINAL_METADATA,
        beta_interfaces.StatusCode.OK, grpc_test_common.DETAILS)

  def metadata_transmitted(self, original_metadata, transmitted_metadata):
    return original_metadata is None or grpc_test_common.metadata_transmitted(
        original_metadata, transmitted_metadata)

  def completion_transmitted(self, original_completion, transmitted_completion):
    if (original_completion.terminal_metadata is not None and
        not grpc_test_common.metadata_transmitted(
            original_completion.terminal_metadata,
            transmitted_completion.terminal_metadata)):
        return False
    elif original_completion.code is not transmitted_completion.code:
      return False
    elif original_completion.message != transmitted_completion.message:
      return False
    else:
      return True


def load_tests(loader, tests, pattern):
  return unittest.TestSuite(
      tests=tuple(
          loader.loadTestsFromTestCase(test_case_class)
          for test_case_class in test_cases.test_cases(_Implementation())))


if __name__ == '__main__':
  unittest.main(verbosity=2)
