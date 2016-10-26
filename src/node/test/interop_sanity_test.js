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
