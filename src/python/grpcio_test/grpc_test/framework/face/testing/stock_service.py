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

"""Examples of Python implementations of the stock.proto Stock service."""

from grpc.framework.common import cardinality
from grpc.framework.foundation import abandonment
from grpc.framework.foundation import stream
from grpc.framework.foundation import stream_util
from grpc_test.framework.face.testing import service
from grpc_test._junkdrawer import stock_pb2

SYMBOL_FORMAT = 'test symbol:%03d'
STREAM_LENGTH = 400

# A test-appropriate security-pricing function. :-P
_price = lambda symbol_name: float(hash(symbol_name) % 4096)


def _get_last_trade_price(stock_request, stock_reply_callback, control, active):
  """A unary-request, unary-response test method."""
  control.control()
  if active():
    stock_reply_callback(
        stock_pb2.StockReply(
            symbol=stock_request.symbol, price=_price(stock_request.symbol)))
  else:
    raise abandonment.Abandoned()


def _get_last_trade_price_multiple(stock_reply_consumer, control, active):
  """A stream-request, stream-response test method."""
  def stock_reply_for_stock_request(stock_request):
    control.control()
    if active():
      return stock_pb2.StockReply(
          symbol=stock_request.symbol, price=_price(stock_request.symbol))
    else:
      raise abandonment.Abandoned()
  return stream_util.TransformingConsumer(
      stock_reply_for_stock_request, stock_reply_consumer)


def _watch_future_trades(stock_request, stock_reply_consumer, control, active):
  """A unary-request, stream-response test method."""
  base_price = _price(stock_request.symbol)
  for index in range(stock_request.num_trades_to_watch):
    control.control()
    if active():
      stock_reply_consumer.consume(
          stock_pb2.StockReply(
              symbol=stock_request.symbol, price=base_price + index))
    else:
      raise abandonment.Abandoned()
  stock_reply_consumer.terminate()


def _get_highest_trade_price(stock_reply_callback, control, active):
  """A stream-request, unary-response test method."""

  class StockRequestConsumer(stream.Consumer):
    """Keeps an ongoing record of the most valuable symbol yet consumed."""

    def __init__(self):
      self._symbol = None
      self._price = None

    def consume(self, stock_request):
      control.control()
      if active():
        if self._price is None:
          self._symbol = stock_request.symbol
          self._price = _price(stock_request.symbol)
        else:
          candidate_price = _price(stock_request.symbol)
          if self._price < candidate_price:
            self._symbol = stock_request.symbol
            self._price = candidate_price

    def terminate(self):
      control.control()
      if active():
        if self._symbol is None:
          raise ValueError()
        else:
          stock_reply_callback(
              stock_pb2.StockReply(symbol=self._symbol, price=self._price))
          self._symbol = None
          self._price = None

    def consume_and_terminate(self, stock_request):
      control.control()
      if active():
        if self._price is None:
          stock_reply_callback(
              stock_pb2.StockReply(
                  symbol=stock_request.symbol,
                  price=_price(stock_request.symbol)))
        else:
          candidate_price = _price(stock_request.symbol)
          if self._price < candidate_price:
            stock_reply_callback(
                stock_pb2.StockReply(
                    symbol=stock_request.symbol, price=candidate_price))
          else:
            stock_reply_callback(
                stock_pb2.StockReply(
                    symbol=self._symbol, price=self._price))

        self._symbol = None
        self._price = None

  return StockRequestConsumer()


class GetLastTradePrice(service.UnaryUnaryTestMethodImplementation):
  """GetLastTradePrice for use in tests."""

  def name(self):
    return 'GetLastTradePrice'

  def cardinality(self):
    return cardinality.Cardinality.UNARY_UNARY

  def request_class(self):
    return stock_pb2.StockRequest

  def response_class(self):
    return stock_pb2.StockReply

  def serialize_request(self, request):
    return request.SerializeToString()

  def deserialize_request(self, serialized_request):
    return stock_pb2.StockRequest.FromString(serialized_request)

  def serialize_response(self, response):
    return response.SerializeToString()

  def deserialize_response(self, serialized_response):
    return stock_pb2.StockReply.FromString(serialized_response)

  def service(self, request, response_callback, context, control):
    _get_last_trade_price(
        request, response_callback, control, context.is_active)


class GetLastTradePriceMessages(service.UnaryUnaryTestMessages):

  def __init__(self):
    self._index = 0

  def request(self):
    symbol = SYMBOL_FORMAT % self._index
    self._index += 1
    return stock_pb2.StockRequest(symbol=symbol)

  def verify(self, request, response, test_case):
    test_case.assertEqual(request.symbol, response.symbol)
    test_case.assertEqual(_price(request.symbol), response.price)


class GetLastTradePriceMultiple(service.StreamStreamTestMethodImplementation):
  """GetLastTradePriceMultiple for use in tests."""

  def name(self):
    return 'GetLastTradePriceMultiple'

  def cardinality(self):
    return cardinality.Cardinality.STREAM_STREAM

  def request_class(self):
    return stock_pb2.StockRequest

  def response_class(self):
    return stock_pb2.StockReply

  def serialize_request(self, request):
    return request.SerializeToString()

  def deserialize_request(self, serialized_request):
    return stock_pb2.StockRequest.FromString(serialized_request)

  def serialize_response(self, response):
    return response.SerializeToString()

  def deserialize_response(self, serialized_response):
    return stock_pb2.StockReply.FromString(serialized_response)

  def service(self, response_consumer, context, control):
    return _get_last_trade_price_multiple(
        response_consumer, control, context.is_active)


class GetLastTradePriceMultipleMessages(service.StreamStreamTestMessages):
  """Pairs of message streams for use with GetLastTradePriceMultiple."""

  def __init__(self):
    self._index = 0

  def requests(self):
    base_index = self._index
    self._index += 1
    return [
        stock_pb2.StockRequest(symbol=SYMBOL_FORMAT % (base_index + index))
        for index in range(STREAM_LENGTH)]

  def verify(self, requests, responses, test_case):
    test_case.assertEqual(len(requests), len(responses))
    for stock_request, stock_reply in zip(requests, responses):
      test_case.assertEqual(stock_request.symbol, stock_reply.symbol)
      test_case.assertEqual(_price(stock_request.symbol), stock_reply.price)


class WatchFutureTrades(service.UnaryStreamTestMethodImplementation):
  """WatchFutureTrades for use in tests."""

  def name(self):
    return 'WatchFutureTrades'

  def cardinality(self):
    return cardinality.Cardinality.UNARY_STREAM

  def request_class(self):
    return stock_pb2.StockRequest

  def response_class(self):
    return stock_pb2.StockReply

  def serialize_request(self, request):
    return request.SerializeToString()

  def deserialize_request(self, serialized_request):
    return stock_pb2.StockRequest.FromString(serialized_request)

  def serialize_response(self, response):
    return response.SerializeToString()

  def deserialize_response(self, serialized_response):
    return stock_pb2.StockReply.FromString(serialized_response)

  def service(self, request, response_consumer, context, control):
    _watch_future_trades(request, response_consumer, control, context.is_active)


class WatchFutureTradesMessages(service.UnaryStreamTestMessages):
  """Pairs of a single request message and a sequence of response messages."""

  def __init__(self):
    self._index = 0

  def request(self):
    symbol = SYMBOL_FORMAT % self._index
    self._index += 1
    return stock_pb2.StockRequest(
        symbol=symbol, num_trades_to_watch=STREAM_LENGTH)

  def verify(self, request, responses, test_case):
    test_case.assertEqual(STREAM_LENGTH, len(responses))
    base_price = _price(request.symbol)
    for index, response in enumerate(responses):
      test_case.assertEqual(base_price + index, response.price)


class GetHighestTradePrice(service.StreamUnaryTestMethodImplementation):
  """GetHighestTradePrice for use in tests."""

  def name(self):
    return 'GetHighestTradePrice'

  def cardinality(self):
    return cardinality.Cardinality.STREAM_UNARY

  def request_class(self):
    return stock_pb2.StockRequest

  def response_class(self):
    return stock_pb2.StockReply

  def serialize_request(self, request):
    return request.SerializeToString()

  def deserialize_request(self, serialized_request):
    return stock_pb2.StockRequest.FromString(serialized_request)

  def serialize_response(self, response):
    return response.SerializeToString()

  def deserialize_response(self, serialized_response):
    return stock_pb2.StockReply.FromString(serialized_response)

  def service(self, response_callback, context, control):
    return _get_highest_trade_price(
        response_callback, control, context.is_active)


class GetHighestTradePriceMessages(service.StreamUnaryTestMessages):

  def requests(self):
    return [
        stock_pb2.StockRequest(symbol=SYMBOL_FORMAT % index)
        for index in range(STREAM_LENGTH)]

  def verify(self, requests, response, test_case):
    price = None
    symbol = None
    for stock_request in requests:
      current_symbol = stock_request.symbol
      current_price = _price(current_symbol)
      if price is None or price < current_price:
        price = current_price
        symbol = current_symbol
    test_case.assertEqual(price, response.price)
    test_case.assertEqual(symbol, response.symbol)


class StockTestService(service.TestService):
  """A corpus of test data with one method of each RPC cardinality."""

  def name(self):
    return 'Stock'

  def unary_unary_scenarios(self):
    return {
        'GetLastTradePrice': (
            GetLastTradePrice(), [GetLastTradePriceMessages()]),
    }

  def unary_stream_scenarios(self):
    return {
        'WatchFutureTrades': (
            WatchFutureTrades(), [WatchFutureTradesMessages()]),
    }

  def stream_unary_scenarios(self):
    return {
        'GetHighestTradePrice': (
            GetHighestTradePrice(), [GetHighestTradePriceMessages()])
    }

  def stream_stream_scenarios(self):
    return {
        'GetLastTradePriceMultiple': (
            GetLastTradePriceMultiple(), [GetLastTradePriceMultipleMessages()]),
    }


STOCK_TEST_SERVICE = StockTestService()
