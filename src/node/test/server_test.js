/*
 *
 * Copyright 2014, Google Inc.
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

var assert = require('assert');
var grpc = require('bindings')('grpc.node');
var Server = require('../server');
var port_picker = require('../port_picker');

/**
 * This is used for testing functions with multiple asynchronous calls that
 * can happen in different orders. This should be passed the number of async
 * function invocations that can occur last, and each of those should call this
 * function's return value
 * @param {function()} done The function that should be called when a test is
 *     complete.
 * @param {number} count The number of calls to the resulting function if the
 *     test passes.
 * @return {function()} The function that should be called at the end of each
 *     sequence of asynchronous functions.
 */
function multiDone(done, count) {
  return function() {
    count -= 1;
    if (count <= 0) {
      done();
    }
  };
}

/**
 * Responds to every request with the same data as a response
 * @param {Stream} stream
 */
function echoHandler(stream) {
  stream.pipe(stream);
}

describe('echo server', function() {
  it('should echo inputs as responses', function(done) {
    done = multiDone(done, 4);
    port_picker.nextAvailablePort(function(port) {
      var server = new Server();
      server.bind(port);
      server.register('echo', echoHandler);
      server.start();

      var req_text = 'echo test string';
      var status_text = 'OK';

      var channel = new grpc.Channel(port);
      var deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + 3);
      var call = new grpc.Call(channel,
                               'echo',
                               deadline);
      call.startInvoke(function(event) {
        assert.strictEqual(event.type,
                           grpc.completionType.INVOKE_ACCEPTED);
        call.startWrite(
            new Buffer(req_text),
            function(event) {
              assert.strictEqual(event.type,
                                 grpc.completionType.WRITE_ACCEPTED);
              assert.strictEqual(event.data, grpc.opError.OK);
              call.writesDone(function(event) {
                assert.strictEqual(event.type,
                                   grpc.completionType.FINISH_ACCEPTED);
                assert.strictEqual(event.data, grpc.opError.OK);
                done();
              });
            }, 0);
        call.startRead(function(event) {
          assert.strictEqual(event.type, grpc.completionType.READ);
          assert.strictEqual(event.data.toString(), req_text);
          done();
        });
      },function(event) {
        assert.strictEqual(event.type,
                           grpc.completionType.CLIENT_METADATA_READ);
        done();
      },function(event) {
        assert.strictEqual(event.type, grpc.completionType.FINISHED);
        var status = event.data;
        assert.strictEqual(status.code, grpc.status.OK);
        assert.strictEqual(status.details, status_text);
        server.shutdown();
        done();
      }, 0);
    });
  });
});
