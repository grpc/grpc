/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

var PROTO_PATH = __dirname + '/../../../proto/math/math.proto';

var grpc = require('grpc');
var protoLoader = require('@grpc/proto-loader');
var packageDefinition = protoLoader.loadSync(
    PROTO_PATH,
    {keepCase: true,
     longs: String,
     enums: String,
     defaults: true,
     oneofs: true
    });
var math_proto = grpc.loadPackageDefinition(packageDefinition).math;

/**
 * Implements the Div RPC method.
 */
function Div(call, callback) {
  var divisor = call.request.divisor;
  var dividend = call.request.dividend;
  if (divisor == 0) {
    callback({
      code: grpc.status.INVALID_ARGUMENT,
      details: 'Cannot divide by zero'
    });
  } else {
    setTimeout(function () {
      callback(null, {
        quotient: Math.floor(dividend / divisor),
        remainder: dividend % divisor
      });
    }, 1); // 1 millisecond, to make sure 1 microsecond timeout from test
           // will hit. TODO: Consider fixing this.
  }
}

/**
 * Implements the Fib RPC method.
 */
function Fib(stream) {
  var previous = 0, current = 1;
  for (var i = 0; i < stream.request.limit; i++) {
    stream.write({
      num: current
    });
    var temp = current;
    current += previous;
    previous = temp;
  }
  stream.end();
}

/**
 * Implements the Sum RPC method.
 */
function Sum(call, callback) {
  var sum = 0;
  call.on('data', function(data) {
    sum += parseInt(data.num);
  });
  call.on('end', function() {
    callback(null, {
      num: sum
    });
  });
}

/**
 * Implements the DivMany RPC method.
 */
function DivMany(stream) {
  stream.on('data', function(div_args) {
    var divisor = div_args.divisor;
    var dividend = div_args.dividend;
    if (divisor == 0) {
      stream.emit('error', {
        code: grpc.status.INVALID_ARGUMENT,
        details: 'Cannot divide by zero'
      });
    } else {
      stream.write({
        quotient: Math.floor(dividend / divisor),
        remainder: dividend % divisor
      });
    }
  });
  stream.on('end', function() {
    stream.end();
  });
}


/**
 * Starts an RPC server that receives requests for the Math service at the
 * sample server port
 */
function main() {
  var server = new grpc.Server();
  server.addService(math_proto.Math.service, {
    Div: Div,
    Fib: Fib,
    Sum: Sum,
    DivMany: DivMany,
  });
  server.bind('0.0.0.0:50052', grpc.ServerCredentials.createInsecure());
  var fs = require('fs');
  var key_data = fs.readFileSync(__dirname + '/../data/server1.key');
  var pem_data = fs.readFileSync(__dirname + '/../data/server1.pem');
  server.bind('0.0.0.0:50051', grpc.ServerCredentials.createSsl(null, [{private_key: key_data,
    cert_chain: pem_data}]));     
  server.start();
}

main();
