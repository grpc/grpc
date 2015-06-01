/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

'use strict';

var _ = require('underscore');
var grpc = require('..');
var examples = grpc.load(__dirname + '/stock.proto').examples;

var StockServer = grpc.buildServer([examples.Stock.service]);

function getLastTradePrice(call, callback) {
  callback(null, {symbol: call.request.symbol, price: 88});
}

function watchFutureTrades(call) {
  for (var i = 0; i < call.request.num_trades_to_watch; i++) {
    call.write({price: 88.00 + i * 10.00});
  }
  call.end();
}

function getHighestTradePrice(call, callback) {
  var trades = [];
  call.on('data', function(data) {
    trades.push({symbol: data.symbol, price: _.random(0, 100)});
  });
  call.on('end', function() {
    if(_.isEmpty(trades)) {
      callback(null, {});
    } else {
      callback(null, _.max(trades, function(trade){return trade.price;}));
    }
  });
}

function getLastTradePriceMultiple(call) {
  call.on('data', function(data) {
    call.write({price: 88});
  });
  call.on('end', function() {
    call.end();
  });
}

var stockServer = new StockServer({
  'examples.Stock' : {
    getLastTradePrice: getLastTradePrice,
    getLastTradePriceMultiple: getLastTradePriceMultiple,
    watchFutureTrades: watchFutureTrades,
    getHighestTradePrice: getHighestTradePrice
  }
});

if (require.main === module) {
  stockServer.bind('0.0.0.0:50051');
  stockServer.listen();
}

module.exports = stockServer;
