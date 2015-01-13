var assert = require('assert');
var fs = require('fs');
var path = require('path');
var grpc = require('bindings')('grpc.node');
var Server = require('../server');
var client = require('../client');
var port_picker = require('../port_picker');
var common = require('../common');
var _ = require('highland');

var ca_path = path.join(__dirname, 'data/ca.pem');

var key_path = path.join(__dirname, 'data/server1.key');

var pem_path = path.join(__dirname, 'data/server1.pem');

/**
 * Helper function to return an absolute deadline given a relative timeout in
 * seconds.
 * @param {number} timeout_secs The number of seconds to wait before timing out
 * @return {Date} A date timeout_secs in the future
 */
function getDeadline(timeout_secs) {
  var deadline = new Date();
  deadline.setSeconds(deadline.getSeconds() + timeout_secs);
  return deadline;
}

/**
 * Responds to every request with the same data as a response
 * @param {Stream} stream
 */
function echoHandler(stream) {
  stream.pipe(stream);
}

/**
 * Responds to every request with an error status
 * @param {Stream} stream
 */
function errorHandler(stream) {
  throw {
    'code' : grpc.status.UNIMPLEMENTED,
    'details' : 'error details'
  };
}

describe('echo client', function() {
  it('should receive echo responses', function(done) {
    port_picker.nextAvailablePort(function(port) {
      var server = new Server();
      server.bind(port);
      server.register('echo', echoHandler);
      server.start();

      var messages = ['echo1', 'echo2', 'echo3', 'echo4'];
      var channel = new grpc.Channel(port);
      var stream = client.makeRequest(
          channel,
          'echo');
      _(messages).map(function(val) {
        return new Buffer(val);
      }).pipe(stream);
      var index = 0;
      stream.on('data', function(chunk) {
        assert.equal(messages[index], chunk.toString());
        index += 1;
      });
      stream.on('end', function() {
        server.shutdown();
        done();
      });
    });
  });
  it('should get an error status that the server throws', function(done) {
    port_picker.nextAvailablePort(function(port) {
      var server = new Server();
      server.bind(port);
      server.register('error', errorHandler);
      server.start();

      var channel = new grpc.Channel(port);
      var stream = client.makeRequest(
          channel,
          'error',
          null,
          getDeadline(1));

      stream.on('data', function() {});
      stream.write(new Buffer('test'));
      stream.end();
      stream.on('status', function(status) {
        assert.equal(status.code, grpc.status.UNIMPLEMENTED);
        assert.equal(status.details, 'error details');
        server.shutdown();
        done();
      });

    });
  });
});
/* TODO(mlumish): explore options for reducing duplication between this test
 * and the insecure echo client test */
describe('secure echo client', function() {
  it('should recieve echo responses', function(done) {
    port_picker.nextAvailablePort(function(port) {
      fs.readFile(ca_path, function(err, ca_data) {
        assert.ifError(err);
        fs.readFile(key_path, function(err, key_data) {
          assert.ifError(err);
          fs.readFile(pem_path, function(err, pem_data) {
            assert.ifError(err);
            var creds = grpc.Credentials.createSsl(ca_data);
            var server_creds = grpc.ServerCredentials.createSsl(null,
                                                                key_data,
                                                                pem_data);

            var server = new Server({'credentials' : server_creds});
            server.bind(port, true);
            server.register('echo', echoHandler);
            server.start();

            var messages = ['echo1', 'echo2', 'echo3', 'echo4'];
            var channel = new grpc.Channel(port, {
              'grpc.ssl_target_name_override' : 'foo.test.google.com',
              'credentials' : creds
            });
            var stream = client.makeRequest(
                channel,
                'echo');

            _(messages).map(function(val) {
              return new Buffer(val);
            }).pipe(stream);
            var index = 0;
            stream.on('data', function(chunk) {
              assert.equal(messages[index], chunk.toString());
              index += 1;
            });
            stream.on('end', function() {
              server.shutdown();
              done();
            });
          });

        });
      });
    });
  });
});
