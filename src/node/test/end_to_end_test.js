var assert = require('assert');
var grpc = require('bindings')('grpc.node');
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

describe('end-to-end', function() {
  it('should start and end a request without error', function(complete) {
    port_picker.nextAvailablePort(function(port) {
      var server = new grpc.Server();
      var done = multiDone(function() {
        complete();
        server.shutdown();
      }, 2);
      server.addHttp2Port(port);
      var channel = new grpc.Channel(port);
      var deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + 3);
      var status_text = 'xyz';
      var call = new grpc.Call(channel,
                               'dummy_method',
                               deadline);
      call.startInvoke(function(event) {
        assert.strictEqual(event.type,
                           grpc.completionType.INVOKE_ACCEPTED);

        call.writesDone(function(event) {
          assert.strictEqual(event.type,
                             grpc.completionType.FINISH_ACCEPTED);
          assert.strictEqual(event.data, grpc.opError.OK);
        });
      },function(event) {
        assert.strictEqual(event.type,
                           grpc.completionType.CLIENT_METADATA_READ);
      },function(event) {
        assert.strictEqual(event.type, grpc.completionType.FINISHED);
        var status = event.data;
        assert.strictEqual(status.code, grpc.status.OK);
        assert.strictEqual(status.details, status_text);
        done();
      }, 0);

      server.start();
      server.requestCall(function(event) {
        assert.strictEqual(event.type, grpc.completionType.SERVER_RPC_NEW);
        var server_call = event.call;
        assert.notEqual(server_call, null);
        server_call.serverAccept(function(event) {
          assert.strictEqual(event.type, grpc.completionType.FINISHED);
        }, 0);
        server_call.serverEndInitialMetadata(0);
        server_call.startWriteStatus(
            grpc.status.OK,
            status_text,
            function(event) {
              assert.strictEqual(event.type,
                                 grpc.completionType.FINISH_ACCEPTED);
              assert.strictEqual(event.data, grpc.opError.OK);
              done();
            });
      });
    });
  });

  it('should send and receive data without error', function(complete) {
    port_picker.nextAvailablePort(function(port) {
      var req_text = 'client_request';
      var reply_text = 'server_response';
      var server = new grpc.Server();
      var done = multiDone(function() {
        complete();
        server.shutdown();
      }, 6);
      server.addHttp2Port(port);
      var channel = new grpc.Channel(port);
      var deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + 3);
      var status_text = 'success';
      var call = new grpc.Call(channel,
                               'dummy_method',
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
          assert.strictEqual(event.data.toString(), reply_text);
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
        done();
      }, 0);

      server.start();
      server.requestCall(function(event) {
        assert.strictEqual(event.type, grpc.completionType.SERVER_RPC_NEW);
        var server_call = event.call;
        assert.notEqual(server_call, null);
        server_call.serverAccept(function(event) {
          assert.strictEqual(event.type, grpc.completionType.FINISHED);
          done();
        });
        server_call.serverEndInitialMetadata(0);
        server_call.startRead(function(event) {
          assert.strictEqual(event.type, grpc.completionType.READ);
          assert.strictEqual(event.data.toString(), req_text);
          server_call.startWrite(
              new Buffer(reply_text),
              function(event) {
                assert.strictEqual(event.type,
                                   grpc.completionType.WRITE_ACCEPTED);
                assert.strictEqual(event.data,
                                   grpc.opError.OK);
                server_call.startWriteStatus(
                    grpc.status.OK,
                    status_text,
                    function(event) {
                      assert.strictEqual(event.type,
                                         grpc.completionType.FINISH_ACCEPTED);
                      assert.strictEqual(event.data, grpc.opError.OK);
                      done();
                    });
              }, 0);
        });
      });
    });
  });
});
