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

import threading
import unittest

from grpc.framework.assembly import implementations
from grpc.framework.assembly import utilities
from grpc.framework.base import interfaces
from grpc.framework.base.packets import packets as tickets
from grpc.framework.base.packets import interfaces as tickets_interfaces
from grpc.framework.base.packets import null
from grpc.framework.foundation import logging_pool
from grpc._junkdrawer import math_pb2

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


_IMPLEMENTATIONS = {
    DIV: utilities.unary_unary_inline(_div),
    DIV_MANY: utilities.stream_stream_inline(_div_many),
    FIB: utilities.unary_stream_inline(_fib),
    SUM: utilities.stream_unary_inline(_sum),
}

_TIMEOUT = 10


class PipeLink(tickets_interfaces.ForeLink, tickets_interfaces.RearLink):

  def __init__(self):
    self._fore_lock = threading.Lock()
    self._fore_link = null.NULL_FORE_LINK
    self._rear_lock = threading.Lock()
    self._rear_link = null.NULL_REAR_LINK

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    return False

  def start(self):
    pass

  def stop(self):
    pass

  def accept_back_to_front_ticket(self, ticket):
    with self._fore_lock:
      self._fore_link.accept_back_to_front_ticket(ticket)

  def join_rear_link(self, rear_link):
    with self._rear_lock:
      self._rear_link = null.NULL_REAR_LINK if rear_link is None else rear_link

  def accept_front_to_back_ticket(self, ticket):
    with self._rear_lock:
      self._rear_link.accept_front_to_back_ticket(ticket)

  def join_fore_link(self, fore_link):
    with self._fore_lock:
      self._fore_link = null.NULL_FORE_LINK if fore_link is None else fore_link


class FaceStubTest(unittest.TestCase):

  def testUnaryUnary(self):
    divisor = 7
    dividend = 13
    expected_quotient = 1
    expected_remainder = 6
    pipe = PipeLink()
    service = implementations.assemble_service(_IMPLEMENTATIONS, pipe)
    face_stub = implementations.assemble_face_stub(pipe)

    service.start()
    try:
      with face_stub:
        response = face_stub.blocking_value_in_value_out(
            DIV, math_pb2.DivArgs(divisor=divisor, dividend=dividend),
            _TIMEOUT)
        self.assertEqual(expected_quotient, response.quotient)
        self.assertEqual(expected_remainder, response.remainder)
    finally:
      service.stop()

  def testUnaryStream(self):
    stream_length = 29
    pipe = PipeLink()
    service = implementations.assemble_service(_IMPLEMENTATIONS, pipe)
    face_stub = implementations.assemble_face_stub(pipe)

    with service, face_stub:
      responses = list(
          face_stub.inline_value_in_stream_out(
              FIB, math_pb2.FibArgs(limit=stream_length), _TIMEOUT))
      numbers = [response.num for response in responses]
      for early, middle, later in zip(numbers, numbers[1:], numbers[2:]):
        self.assertEqual(early + middle, later)

  def testStreamUnary(self):
    stream_length = 13
    pipe = PipeLink()
    service = implementations.assemble_service(_IMPLEMENTATIONS, pipe)
    face_stub = implementations.assemble_face_stub(pipe)

    with service, face_stub:
      sync_async = face_stub.stream_unary_sync_async(SUM)
      response_future = sync_async.async(
          (math_pb2.Num(num=index) for index in range(stream_length)),
          _TIMEOUT)
      self.assertEqual(
          (stream_length * (stream_length - 1)) / 2,
          response_future.result().num)

  def testStreamStream(self):
    stream_length = 17
    divisor_offset = 7
    dividend_offset = 17
    pipe = PipeLink()
    service = implementations.assemble_service(_IMPLEMENTATIONS, pipe)
    face_stub = implementations.assemble_face_stub(pipe)

    with service, face_stub:
      response_iterator = face_stub.inline_stream_in_stream_out(
          DIV_MANY,
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


class DynamicInlineStubTest(unittest.TestCase):

  def testUnaryUnary(self):
    divisor = 59
    dividend = 973
    expected_quotient = dividend / divisor
    expected_remainder = dividend % divisor
    pipe = PipeLink()
    service = implementations.assemble_service(_IMPLEMENTATIONS, pipe)
    dynamic_stub = implementations.assemble_dynamic_inline_stub(
        _IMPLEMENTATIONS, pipe)

    service.start()
    with dynamic_stub:
      response = dynamic_stub.Div(
          math_pb2.DivArgs(divisor=divisor, dividend=dividend), _TIMEOUT)
      self.assertEqual(expected_quotient, response.quotient)
      self.assertEqual(expected_remainder, response.remainder)
    service.stop()

  def testUnaryStream(self):
    stream_length = 43
    pipe = PipeLink()
    service = implementations.assemble_service(_IMPLEMENTATIONS, pipe)
    dynamic_stub = implementations.assemble_dynamic_inline_stub(
        _IMPLEMENTATIONS, pipe)

    with service, dynamic_stub:
      response_iterator = dynamic_stub.Fib(
          math_pb2.FibArgs(limit=stream_length), _TIMEOUT)
      numbers = tuple(response.num for response in response_iterator)
      for early, middle, later in zip(numbers, numbers[:1], numbers[:2]):
        self.assertEqual(early + middle, later)
      self.assertEqual(stream_length, len(numbers))

  def testStreamUnary(self):
    stream_length = 127
    pipe = PipeLink()
    service = implementations.assemble_service(_IMPLEMENTATIONS, pipe)
    dynamic_stub = implementations.assemble_dynamic_inline_stub(
        _IMPLEMENTATIONS, pipe)

    with service, dynamic_stub:
      response_future = dynamic_stub.Sum.async(
          (math_pb2.Num(num=index) for index in range(stream_length)),
          _TIMEOUT)
      self.assertEqual(
          (stream_length * (stream_length - 1)) / 2,
          response_future.result().num)

  def testStreamStream(self):
    stream_length = 179
    divisor_offset = 71
    dividend_offset = 1763
    pipe = PipeLink()
    service = implementations.assemble_service(_IMPLEMENTATIONS, pipe)
    dynamic_stub = implementations.assemble_dynamic_inline_stub(
        _IMPLEMENTATIONS, pipe)

    with service, dynamic_stub:
      response_iterator = dynamic_stub.DivMany(
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
  unittest.main()
