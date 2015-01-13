var assert = require('assert');
var grpc = require('bindings')('grpc.node');

describe('channel', function() {
  describe('constructor', function() {
    it('should require a string for the first argument', function() {
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname');
      });
      assert.throws(function() {
        new grpc.Channel();
      }, TypeError);
      assert.throws(function() {
        new grpc.Channel(5);
      });
    });
    it('should accept an object for the second parameter', function() {
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', {});
      });
      assert.throws(function() {
        new grpc.Channel('hostname', 5);
      });
    });
    it('should only accept objects with string or int values', function() {
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', {'key' : 'value'});
      });
      assert.doesNotThrow(function() {
        new grpc.Channel('hostname', {'key' : 5});
      });
      assert.throws(function() {
        new grpc.Channel('hostname', {'key' : null});
      });
      assert.throws(function() {
        new grpc.Channel('hostname', {'key' : new Date()});
      });
    });
  });
  describe('close', function() {
    it('should succeed silently', function() {
      var channel = new grpc.Channel('hostname', {});
      assert.doesNotThrow(function() {
        channel.close();
      });
    });
    it('should be idempotent', function() {
      var channel = new grpc.Channel('hostname', {});
      assert.doesNotThrow(function() {
        channel.close();
        channel.close();
      });
    });
  });
});
