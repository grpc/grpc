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

'use strict';

var interop_server = require('../interop/interop_server.js');
var interop_client = require('../interop/interop_client.js');

var server;

var port;

var name_override = 'foo.test.google.fr';

describe('Interop tests', function() {
  before(function(done) {
    var server_obj = interop_server.getServer(0, true);
    server = server_obj.server;
    server.start();
    port = 'localhost:' + server_obj.port;
    done();
  });
  after(function() {
    server.forceShutdown();
  });
  // This depends on not using a binary stream
  it('should pass empty_unary', function(done) {
    interop_client.runTest(port, name_override, 'empty_unary', true, true,
                           done);
  });
  // This fails due to an unknown bug
  it('should pass large_unary', function(done) {
    interop_client.runTest(port, name_override, 'large_unary', true, true,
                           done);
  });
  it('should pass client_streaming', function(done) {
    interop_client.runTest(port, name_override, 'client_streaming', true, true,
                           done);
  });
  it('should pass server_streaming', function(done) {
    interop_client.runTest(port, name_override, 'server_streaming', true, true,
                           done);
  });
  it('should pass ping_pong', function(done) {
    interop_client.runTest(port, name_override, 'ping_pong', true, true, done);
  });
  it('should pass empty_stream', function(done) {
    interop_client.runTest(port, name_override, 'empty_stream', true, true,
                           done);
  });
  it('should pass cancel_after_begin', function(done) {
    interop_client.runTest(port, name_override, 'cancel_after_begin', true,
                           true, done);
  });
  it('should pass cancel_after_first_response', function(done) {
    interop_client.runTest(port, name_override, 'cancel_after_first_response',
                           true, true, done);
  });
  it('should pass timeout_on_sleeping_server', function(done) {
    interop_client.runTest(port, name_override, 'timeout_on_sleeping_server',
                           true, true, done);
  });
  it('should pass custom_metadata', function(done) {
    interop_client.runTest(port, name_override, 'custom_metadata',
                           true, true, done);
  });
  it('should pass status_code_and_message', function(done) {
    interop_client.runTest(port, name_override, 'status_code_and_message',
                           true, true, done);
  });
  it('should pass unimplemented_service', function(done) {
    interop_client.runTest(port, name_override, 'unimplemented_service',
                           true, true, done);
  });
  it('should pass unimplemented_method', function(done) {
    interop_client.runTest(port, name_override, 'unimplemented_method',
                           true, true, done);
  });
});
