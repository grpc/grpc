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
