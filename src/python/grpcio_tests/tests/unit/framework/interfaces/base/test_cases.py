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

"""Tests of the base interface of RPC Framework."""

from __future__ import division

import logging
import random
import threading
import time
import unittest

from grpc.framework.foundation import logging_pool
from grpc.framework.interfaces.base import base
from grpc.framework.interfaces.base import utilities
from tests.unit.framework.common import test_constants
from tests.unit.framework.interfaces.base import _control
from tests.unit.framework.interfaces.base import test_interfaces

_SYNCHRONICITY_VARIATION = (('Sync', False), ('Async', True))

_EMPTY_OUTCOME_KIND_DICT = {
    outcome_kind: 0 for outcome_kind in base.Outcome.Kind}


class _Serialization(test_interfaces.Serialization):

  def serialize_request(self, request):
    return request + request

  def deserialize_request(self, serialized_request):
    return serialized_request[:len(serialized_request) // 2]

  def serialize_response(self, response):
    return response * 3

  def deserialize_response(self, serialized_response):
    return serialized_response[2 * len(serialized_response) // 3:]


def _advance(quadruples, operator, controller):
  try:
    for quadruple in quadruples:
      operator.advance(
          initial_metadata=quadruple[0], payload=quadruple[1],
          completion=quadruple[2], allowance=quadruple[3])
  except Exception as e:  # pylint: disable=broad-except
    controller.failed('Exception on advance: %e' % e)


class _Operator(base.Operator):

  def __init__(self, controller, on_advance, pool, operator_under_test):
    self._condition = threading.Condition()
    self._controller = controller
    self._on_advance = on_advance
    self._pool = pool
    self._operator_under_test = operator_under_test
    self._pending_advances = []

  def set_operator_under_test(self, operator_under_test):
    with self._condition:
      self._operator_under_test = operator_under_test
      pent_advances = self._pending_advances
      self._pending_advances = []
      pool = self._pool
      controller = self._controller

    if pool is None:
      _advance(pent_advances, operator_under_test, controller)
    else:
      pool.submit(_advance, pent_advances, operator_under_test, controller)

  def advance(
      self, initial_metadata=None, payload=None, completion=None,
      allowance=None):
    on_advance = self._on_advance(
        initial_metadata, payload, completion, allowance)
    if on_advance.kind is _control.OnAdvance.Kind.ADVANCE:
      with self._condition:
        pool = self._pool
        operator_under_test = self._operator_under_test
        controller = self._controller

      quadruple = (
          on_advance.initial_metadata, on_advance.payload,
          on_advance.completion, on_advance.allowance)
      if pool is None:
        _advance((quadruple,), operator_under_test, controller)
      else:
        pool.submit(_advance, (quadruple,), operator_under_test, controller)
    elif on_advance.kind is _control.OnAdvance.Kind.DEFECT:
      raise ValueError(
          'Deliberately raised exception from Operator.advance (in a test)!')


class _ProtocolReceiver(base.ProtocolReceiver):

  def __init__(self):
    self._condition = threading.Condition()
    self._contexts = []

  def context(self, protocol_context):
    with self._condition:
      self._contexts.append(protocol_context)


class _Servicer(base.Servicer):
  """A base.Servicer with instrumented for testing."""

  def __init__(self, group, method, controllers, pool):
    self._condition = threading.Condition()
    self._group = group
    self._method = method
    self._pool = pool
    self._controllers = list(controllers)

  def service(self, group, method, context, output_operator):
    with self._condition:
      controller = self._controllers.pop(0)
      if group != self._group or method != self._method:
        controller.fail(
            '%s != %s or %s != %s' % (group, self._group, method, self._method))
        raise base.NoSuchMethodError(None, None)
      else:
        operator = _Operator(
            controller, controller.on_service_advance, self._pool,
            output_operator)
        outcome = context.add_termination_callback(
            controller.service_on_termination)
        if outcome is not None:
          controller.service_on_termination(outcome)
        return utilities.full_subscription(operator, _ProtocolReceiver())


class _OperationTest(unittest.TestCase):

  def setUp(self):
    if self._synchronicity_variation:
      self._pool = logging_pool.pool(test_constants.POOL_SIZE)
    else:
      self._pool = None
    self._controller = self._controller_creator.controller(
        self._implementation, self._randomness)

  def tearDown(self):
    if self._synchronicity_variation:
      self._pool.shutdown(wait=True)
    else:
      self._pool = None

  def test_operation(self):
    invocation = self._controller.invocation()
    if invocation.subscription_kind is base.Subscription.Kind.FULL:
      test_operator = _Operator(
          self._controller, self._controller.on_invocation_advance,
          self._pool, None)
      subscription = utilities.full_subscription(
          test_operator, _ProtocolReceiver())
    else:
      # TODO(nathaniel): support and test other subscription kinds.
      self.fail('Non-full subscriptions not yet supported!')

    servicer = _Servicer(
        invocation.group, invocation.method, (self._controller,), self._pool)

    invocation_end, service_end, memo = self._implementation.instantiate(
        {(invocation.group, invocation.method): _Serialization()}, servicer)

    try:
      invocation_end.start()
      service_end.start()
      operation_context, operator_under_test = invocation_end.operate(
          invocation.group, invocation.method, subscription, invocation.timeout,
          initial_metadata=invocation.initial_metadata, payload=invocation.payload,
          completion=invocation.completion)
      test_operator.set_operator_under_test(operator_under_test)
      outcome = operation_context.add_termination_callback(
          self._controller.invocation_on_termination)
      if outcome is not None:
        self._controller.invocation_on_termination(outcome)
    except Exception as e:  # pylint: disable=broad-except
      self._controller.failed('Exception on invocation: %s' % e)
      self.fail(e)

    while True:
      instruction = self._controller.poll()
      if instruction.kind is _control.Instruction.Kind.ADVANCE:
        try:
          test_operator.advance(
              *instruction.advance_args, **instruction.advance_kwargs)
        except Exception as e:  # pylint: disable=broad-except
          self._controller.failed('Exception on instructed advance: %s' % e)
      elif instruction.kind is _control.Instruction.Kind.CANCEL:
        try:
          operation_context.cancel()
        except Exception as e:  # pylint: disable=broad-except
          self._controller.failed('Exception on cancel: %s' % e)
      elif instruction.kind is _control.Instruction.Kind.CONCLUDE:
        break

    invocation_stop_event = invocation_end.stop(0)
    service_stop_event = service_end.stop(0)
    invocation_stop_event.wait()
    service_stop_event.wait()
    invocation_stats = invocation_end.operation_stats()
    service_stats = service_end.operation_stats()

    self._implementation.destantiate(memo)

    self.assertTrue(
        instruction.conclude_success, msg=instruction.conclude_message)

    expected_invocation_stats = dict(_EMPTY_OUTCOME_KIND_DICT)
    expected_invocation_stats[
        instruction.conclude_invocation_outcome_kind] += 1
    self.assertDictEqual(expected_invocation_stats, invocation_stats)
    expected_service_stats = dict(_EMPTY_OUTCOME_KIND_DICT)
    expected_service_stats[instruction.conclude_service_outcome_kind] += 1
    self.assertDictEqual(expected_service_stats, service_stats)


def test_cases(implementation):
  """Creates unittest.TestCase classes for a given Base implementation.

  Args:
    implementation: A test_interfaces.Implementation specifying creation and
      destruction of the Base implementation under test.

  Returns:
    A sequence of subclasses of unittest.TestCase defining tests of the
      specified Base layer implementation.
  """
  random_seed = hash(time.time())
  logging.warning('Random seed for this execution: %s', random_seed)
  randomness = random.Random(x=random_seed)

  test_case_classes = []
  for synchronicity_variation in _SYNCHRONICITY_VARIATION:
    for controller_creator in _control.CONTROLLER_CREATORS:
      name = ''.join(
          (synchronicity_variation[0], controller_creator.name(), 'Test',))
      test_case_classes.append(
          type(name, (_OperationTest,),
               {'_implementation': implementation,
                '_randomness': randomness,
                '_synchronicity_variation': synchronicity_variation[1],
                '_controller_creator': controller_creator,
                '__module__': implementation.__module__,
               }))

  return test_case_classes
