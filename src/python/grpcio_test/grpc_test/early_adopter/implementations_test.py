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

# TODO(nathaniel): Expand this test coverage.

"""Test of the GRPC-backed ForeLink and RearLink."""

import unittest

from grpc.early_adopter import implementations
from grpc.framework.alpha import utilities
from grpc_test._junkdrawer import math_pb2

SERVICE_NAME = 'math.Math'

DIV = 'Div'
DIV_MANY = 'DivMany'
FIB = 'Fib'
SUM = 'Sum'

def _fibbonacci(limit):
  left, right = 0, 1
  for _ in xrange(limit):
    yield left
    left, right = right, left + right


def _div(request, unused_context):
  return math_pb2.DivReply(
      quotient=request.dividend / request.divisor,
      remainder=request.dividend % request.divisor)


def _div_many(request_iterator, unused_context):
  for request in request_iterator:
    yield math_pb2.DivReply(
        quotient=request.dividend / request.divisor,
        remainder=request.dividend % request.divisor)


def _fib(request, unused_context):
  for number in _fibbonacci(request.limit):
    yield math_pb2.Num(num=number)


def _sum(request_iterator, unused_context):
  accumulation = 0
  for request in request_iterator:
    accumulation += request.num
  return math_pb2.Num(num=accumulation)


_INVOCATION_DESCRIPTIONS = {
    DIV: utilities.unary_unary_invocation_description(
        math_pb2.DivArgs.SerializeToString, math_pb2.DivReply.FromString),
    DIV_MANY: utilities.stream_stream_invocation_description(
        math_pb2.DivArgs.SerializeToString, math_pb2.DivReply.FromString),
    FIB: utilities.unary_stream_invocation_description(
        math_pb2.FibArgs.SerializeToString, math_pb2.Num.FromString),
    SUM: utilities.stream_unary_invocation_description(
        math_pb2.Num.SerializeToString, math_pb2.Num.FromString),
}

_SERVICE_DESCRIPTIONS = {
    DIV: utilities.unary_unary_service_description(
        _div, math_pb2.DivArgs.FromString,
        math_pb2.DivReply.SerializeToString),
    DIV_MANY: utilities.stream_stream_service_description(
        _div_many, math_pb2.DivArgs.FromString,
        math_pb2.DivReply.SerializeToString),
    FIB: utilities.unary_stream_service_description(
        _fib, math_pb2.FibArgs.FromString, math_pb2.Num.SerializeToString),
    SUM: utilities.stream_unary_service_description(
        _sum, math_pb2.Num.FromString, math_pb2.Num.SerializeToString),
}

_TIMEOUT = 3


class EarlyAdopterImplementationsTest(unittest.TestCase):

  def setUp(self):
    self.server = implementations.server(
        SERVICE_NAME, _SERVICE_DESCRIPTIONS, 0)
    self.server.start()
    port = self.server.port()
    self.stub = implementations.stub(
        SERVICE_NAME, _INVOCATION_DESCRIPTIONS, 'localhost', port)

  def tearDown(self):
    self.server.stop()

  def testUpAndDown(self):
    with self.stub:
      pass

  def testUnaryUnary(self):
    divisor = 59
    dividend = 973
    expected_quotient = dividend / divisor
    expected_remainder = dividend % divisor

    with self.stub:
      response = self.stub.Div(
          math_pb2.DivArgs(divisor=divisor, dividend=dividend), _TIMEOUT)
      self.assertEqual(expected_quotient, response.quotient)
      self.assertEqual(expected_remainder, response.remainder)

  def testUnaryStream(self):
    stream_length = 43

    with self.stub:
      response_iterator = self.stub.Fib(
          math_pb2.FibArgs(limit=stream_length), _TIMEOUT)
      numbers = tuple(response.num for response in response_iterator)
      for early, middle, later in zip(numbers, numbers[:1], numbers[:2]):
        self.assertEqual(early + middle, later)
      self.assertEqual(stream_length, len(numbers))

  def testStreamUnary(self):
    stream_length = 127

    with self.stub:
      response_future = self.stub.Sum.async(
          (math_pb2.Num(num=index) for index in range(stream_length)),
          _TIMEOUT)
      self.assertEqual(
          (stream_length * (stream_length - 1)) / 2,
          response_future.result().num)

  def testStreamStream(self):
    stream_length = 179
    divisor_offset = 71
    dividend_offset = 1763

    with self.stub:
      response_iterator = self.stub.DivMany(
          (math_pb2.DivArgs(
               divisor=divisor_offset + index,
               dividend=dividend_offset + index)
           for index in range(stream_length)),
          _TIMEOUT)
      for index, response in enumerate(response_iterator):
        self.assertEqual(
            (dividend_offset + index) / (divisor_offset + index),
            response.quotient)
        self.assertEqual(
            (dividend_offset + index) % (divisor_offset + index),
            response.remainder)
      self.assertEqual(stream_length, index + 1)


if __name__ == '__main__':
  unittest.main(verbosity=2)
