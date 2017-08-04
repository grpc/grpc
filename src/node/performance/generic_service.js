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

var _ = require('lodash');

module.exports = {
  'unaryCall' : {
    path: '/grpc.testing.BenchmarkService/UnaryCall',
    requestStream: false,
    responseStream: false,
    requestSerialize: _.identity,
    requestDeserialize: _.identity,
    responseSerialize: _.identity,
    responseDeserialize: _.identity
  },
  'streamingCall' : {
    path: '/grpc.testing.BenchmarkService/StreamingCall',
    requestStream: true,
    responseStream: true,
    requestSerialize: _.identity,
    requestDeserialize: _.identity,
    responseSerialize: _.identity,
    responseDeserialize: _.identity
  }
};
