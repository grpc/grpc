/*
 *
 * Copyright 2016 gRPC authors.
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

'use strict';

var grpc = require('../../..');

var proto = grpc.load(__dirname + '/../../proto/grpc/testing/metrics.proto');
var metrics = proto.grpc.testing;

function main() {
  var parseArgs = require('minimist');
  var argv = parseArgs(process.argv, {
    string: 'metrics_server_address',
    boolean: 'total_only'
  });
  var client = new metrics.MetricsService(argv.metrics_server_address,
                                          grpc.credentials.createInsecure());
  if (argv.total_only) {
    client.getGauge({name: 'qps'}, function(err, data) {
      console.log(data.name + ':', data.long_value);
    });
  } else {
    var call = client.getAllGauges({});
    call.on('data', function(data) {
      console.log(data.name + ':', data.long_value);
    });
  }
}

main();
