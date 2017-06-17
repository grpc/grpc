#!/usr/bin/env node
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

var grpc = require('grpc');

function identity(x) {
  return x;
}

var Client = grpc.makeGenericClientConstructor({
  'echo' : {
    path: '/buffer/echo',
    requestStream: false,
    responseStream: false,
    requestSerialize: identity,
    requestDeserialize: identity,
    responseSerialize: identity,
    responseDeserialize: identity
  }
});

var client = new Client("localhost:1000", grpc.credentials.createInsecure());

client.$channel.close();

console.log("Success!");
