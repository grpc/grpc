var assert = require('assert');
var grpc = require('bindings')('grpc.node');

/**
 * List of all status names
 * @const
 * @type {Array.<string>}
 */
var statusNames = [
  'OK',
  'CANCELLED',
  'UNKNOWN',
  'INVALID_ARGUMENT',
  'DEADLINE_EXCEEDED',
  'NOT_FOUND',
  'ALREADY_EXISTS',
  'PERMISSION_DENIED',
  'UNAUTHENTICATED',
  'RESOURCE_EXHAUSTED',
  'FAILED_PRECONDITION',
  'ABORTED',
  'OUT_OF_RANGE',
  'UNIMPLEMENTED',
  'INTERNAL',
  'UNAVAILABLE',
  'DATA_LOSS'
];

/**
 * List of all call error names
 * @const
 * @type {Array.<string>}
 */
var callErrorNames = [
  'OK',
  'ERROR',
  'NOT_ON_SERVER',
  'NOT_ON_CLIENT',
  'ALREADY_INVOKED',
  'NOT_INVOKED',
  'ALREADY_FINISHED',
  'TOO_MANY_OPERATIONS',
  'INVALID_FLAGS'
];

/**
 * List of all op error names
 * @const
 * @type {Array.<string>}
 */
var opErrorNames = [
  'OK',
  'ERROR'
];

/**
 * List of all completion type names
 * @const
 * @type {Array.<string>}
 */
var completionTypeNames = [
  'QUEUE_SHUTDOWN',
  'READ',
  'INVOKE_ACCEPTED',
  'WRITE_ACCEPTED',
  'FINISH_ACCEPTED',
  'CLIENT_METADATA_READ',
  'FINISHED',
  'SERVER_RPC_NEW'
];

describe('constants', function() {
  it('should have all of the status constants', function() {
    for (var i = 0; i < statusNames.length; i++) {
      assert(grpc.status.hasOwnProperty(statusNames[i]),
             'status missing: ' + statusNames[i]);
    }
  });
  it('should have all of the call errors', function() {
    for (var i = 0; i < callErrorNames.length; i++) {
      assert(grpc.callError.hasOwnProperty(callErrorNames[i]),
             'call error missing: ' + callErrorNames[i]);
    }
  });
  it('should have all of the op errors', function() {
    for (var i = 0; i < opErrorNames.length; i++) {
      assert(grpc.opError.hasOwnProperty(opErrorNames[i]),
             'op error missing: ' + opErrorNames[i]);
    }
  });
  it('should have all of the completion types', function() {
    for (var i = 0; i < completionTypeNames.length; i++) {
      assert(grpc.completionType.hasOwnProperty(completionTypeNames[i]),
             'completion type missing: ' + completionTypeNames[i]);
    }
  });
});
