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

"""Abstract tests against the interfaces of the base layer of RPC Framework."""

import threading
import time

from grpc.framework.base import interfaces
from grpc.framework.base import util
from grpc.framework.foundation import stream
from grpc.framework.foundation import stream_util
from grpc_test.framework.foundation import stream_testing

TICK = 0.1
SMALL_TIMEOUT = TICK * 50
STREAM_LENGTH = 100

SYNCHRONOUS_ECHO = 'synchronous echo'
ASYNCHRONOUS_ECHO = 'asynchronous echo'
IMMEDIATE_FAILURE = 'immediate failure'
TRIGGERED_FAILURE = 'triggered failure'
WAIT_ON_CONDITION = 'wait on condition'

EMPTY_OUTCOME_DICT = {
    interfaces.Outcome.COMPLETED: 0,
    interfaces.Outcome.CANCELLED: 0,
    interfaces.Outcome.EXPIRED: 0,
    interfaces.Outcome.RECEPTION_FAILURE: 0,
    interfaces.Outcome.TRANSMISSION_FAILURE: 0,
    interfaces.Outcome.SERVICER_FAILURE: 0,
    interfaces.Outcome.SERVICED_FAILURE: 0,
    }


def _synchronous_echo(output_consumer):
  return stream_util.TransformingConsumer(lambda x: x, output_consumer)


class AsynchronousEcho(stream.Consumer):
  """A stream.Consumer that echoes its input to another stream.Consumer."""

  def __init__(self, output_consumer, pool):
    self._lock = threading.Lock()
    self._output_consumer = output_consumer
    self._pool = pool

    self._queue = []
    self._spinning = False

  def _spin(self, value, complete):
    while True:
      if value:
        if complete:
          self._output_consumer.consume_and_terminate(value)
        else:
          self._output_consumer.consume(value)
      elif complete:
        self._output_consumer.terminate()
      with self._lock:
        if self._queue:
          value, complete = self._queue.pop(0)
        else:
          self._spinning = False
          return

  def consume(self, value):
    with self._lock:
      if self._spinning:
        self._queue.append((value, False))
      else:
        self._spinning = True
        self._pool.submit(self._spin, value, False)

  def terminate(self):
    with self._lock:
      if self._spinning:
        self._queue.append((None, True))
      else:
        self._spinning = True
        self._pool.submit(self._spin, None, True)

  def consume_and_terminate(self, value):
    with self._lock:
      if self._spinning:
        self._queue.append((value, True))
      else:
        self._spinning = True
        self._pool.submit(self._spin, value, True)


class TestServicer(interfaces.Servicer):
  """An interfaces.Servicer with instrumented for testing."""

  def __init__(self, pool):
    self._pool = pool
    self.condition = threading.Condition()
    self._released = False

  def service(self, name, context, output_consumer):
    if name == SYNCHRONOUS_ECHO:
      return _synchronous_echo(output_consumer)
    elif name == ASYNCHRONOUS_ECHO:
      return AsynchronousEcho(output_consumer, self._pool)
    elif name == IMMEDIATE_FAILURE:
      raise ValueError()
    elif name == TRIGGERED_FAILURE:
      raise NotImplementedError
    elif name == WAIT_ON_CONDITION:
      with self.condition:
        while not self._released:
          self.condition.wait()
      return _synchronous_echo(output_consumer)
    else:
      raise NotImplementedError()

  def release(self):
    with self.condition:
      self._released = True
      self.condition.notify_all()


class EasyServicedIngestor(interfaces.ServicedIngestor):
  """A trivial implementation of interfaces.ServicedIngestor."""

  def __init__(self, consumer):
    self._consumer = consumer

  def consumer(self, operation_context):
    """See interfaces.ServicedIngestor.consumer for specification."""
    return self._consumer


class FrontAndBackTest(object):
  """A test suite usable against any joined Front and Back."""

  # Pylint doesn't know that this is a unittest.TestCase mix-in.
  # pylint: disable=invalid-name

  def testSimplestCall(self):
    """Tests the absolute simplest call - a one-ticket fire-and-forget."""
    self.front.operate(
        SYNCHRONOUS_ECHO, None, True, SMALL_TIMEOUT,
        util.none_serviced_subscription(), 'test trace ID')
    util.wait_for_idle(self.front)
    self.assertEqual(
        1, self.front.operation_stats()[interfaces.Outcome.COMPLETED])

    # Assuming nothing really pathological (such as pauses on the order of
    # SMALL_TIMEOUT interfering with this test) there are a two different ways
    # the back could have experienced execution up to this point:
    # (1) The ticket is still either in the front waiting to be transmitted
    # or is somewhere on the link between the front and the back. The back has
    # no idea that this test is even happening. Calling wait_for_idle on it
    # would do no good because in this case the back is idle and the call would
    # return with the ticket bound for it still in the front or on the link.
    back_operation_stats = self.back.operation_stats()
    first_back_possibility = EMPTY_OUTCOME_DICT
    # (2) The ticket arrived at the back and the back completed the operation.
    second_back_possibility = dict(EMPTY_OUTCOME_DICT)
    second_back_possibility[interfaces.Outcome.COMPLETED] = 1
    self.assertIn(
        back_operation_stats, (first_back_possibility, second_back_possibility))
    # It's true that if the ticket had arrived at the back and the back had
    # begun processing that wait_for_idle could hold test execution until the
    # back completed the operation, but that doesn't really collapse the
    # possibility space down to one solution.

  def testEntireEcho(self):
    """Tests a very simple one-ticket-each-way round-trip."""
    test_payload = 'test payload'
    test_consumer = stream_testing.TestConsumer()
    subscription = util.full_serviced_subscription(
        EasyServicedIngestor(test_consumer))

    self.front.operate(
        ASYNCHRONOUS_ECHO, test_payload, True, SMALL_TIMEOUT, subscription,
        'test trace ID')

    util.wait_for_idle(self.front)
    util.wait_for_idle(self.back)
    self.assertEqual(
        1, self.front.operation_stats()[interfaces.Outcome.COMPLETED])
    self.assertEqual(
        1, self.back.operation_stats()[interfaces.Outcome.COMPLETED])
    self.assertListEqual([(test_payload, True)], test_consumer.calls)

  def testBidirectionalStreamingEcho(self):
    """Tests sending multiple tickets each way."""
    test_payload_template = 'test_payload: %03d'
    test_payloads = [test_payload_template % i for i in range(STREAM_LENGTH)]
    test_consumer = stream_testing.TestConsumer()
    subscription = util.full_serviced_subscription(
        EasyServicedIngestor(test_consumer))

    operation = self.front.operate(
        SYNCHRONOUS_ECHO, None, False, SMALL_TIMEOUT, subscription,
        'test trace ID')

    for test_payload in test_payloads:
      operation.consumer.consume(test_payload)
    operation.consumer.terminate()

    util.wait_for_idle(self.front)
    util.wait_for_idle(self.back)
    self.assertEqual(
        1, self.front.operation_stats()[interfaces.Outcome.COMPLETED])
    self.assertEqual(
        1, self.back.operation_stats()[interfaces.Outcome.COMPLETED])
    self.assertListEqual(test_payloads, test_consumer.values())

  def testCancellation(self):
    """Tests cancelling a long-lived operation."""
    test_consumer = stream_testing.TestConsumer()
    subscription = util.full_serviced_subscription(
        EasyServicedIngestor(test_consumer))

    operation = self.front.operate(
        ASYNCHRONOUS_ECHO, None, False, SMALL_TIMEOUT, subscription,
        'test trace ID')
    operation.cancel()

    util.wait_for_idle(self.front)
    self.assertEqual(
        1, self.front.operation_stats()[interfaces.Outcome.CANCELLED])
    util.wait_for_idle(self.back)
    self.assertListEqual([], test_consumer.calls)

    # Assuming nothing really pathological (such as pauses on the order of
    # SMALL_TIMEOUT interfering with this test) there are a two different ways
    # the back could have experienced execution up to this point:
    # (1) Both tickets are still either in the front waiting to be transmitted
    # or are somewhere on the link between the front and the back. The back has
    # no idea that this test is even happening. Calling wait_for_idle on it
    # would do no good because in this case the back is idle and the call would
    # return with the tickets bound for it still in the front or on the link.
    back_operation_stats = self.back.operation_stats()
    first_back_possibility = EMPTY_OUTCOME_DICT
    # (2) Both tickets arrived within SMALL_TIMEOUT of one another at the back.
    # The back started processing based on the first ticket and then stopped
    # upon receiving the cancellation ticket.
    second_back_possibility = dict(EMPTY_OUTCOME_DICT)
    second_back_possibility[interfaces.Outcome.CANCELLED] = 1
    self.assertIn(
        back_operation_stats, (first_back_possibility, second_back_possibility))

  def testExpiration(self):
    """Tests that operations time out."""
    timeout = TICK * 2
    allowance = TICK  # How much extra time to
    condition = threading.Condition()
    test_payload = 'test payload'
    subscription = util.termination_only_serviced_subscription()
    start_time = time.time()

    outcome_cell = [None]
    termination_time_cell = [None]
    def termination_action(outcome):
      with condition:
        outcome_cell[0] = outcome
        termination_time_cell[0] = time.time()
        condition.notify()

    with condition:
      operation = self.front.operate(
          SYNCHRONOUS_ECHO, test_payload, False, timeout, subscription,
          'test trace ID')
      operation.context.add_termination_callback(termination_action)
      while outcome_cell[0] is None:
        condition.wait()

    duration = termination_time_cell[0] - start_time
    self.assertLessEqual(timeout, duration)
    self.assertLess(duration, timeout + allowance)
    self.assertEqual(interfaces.Outcome.EXPIRED, outcome_cell[0])
    util.wait_for_idle(self.front)
    self.assertEqual(
        1, self.front.operation_stats()[interfaces.Outcome.EXPIRED])
    util.wait_for_idle(self.back)
    self.assertLessEqual(
        1, self.back.operation_stats()[interfaces.Outcome.EXPIRED])
