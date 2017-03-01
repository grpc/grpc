/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

'use strict';

var assert = require('assert');
var grpc = require('../src/grpc_extension');

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
 * List of all propagate flag names
 * @const
 * @type {Array.<string>}
 */
var propagateFlagNames = [
  'DEADLINE',
  'CENSUS_STATS_CONTEXT',
  'CENSUS_TRACING_CONTEXT',
  'CANCELLATION',
  'DEFAULTS'
];
/*
 * List of all connectivity state names
 * @const
 * @type {Array.<string>}
 */
var connectivityStateNames = [
  'IDLE',
  'CONNECTING',
  'READY',
  'TRANSIENT_FAILURE',
  'FATAL_FAILURE'
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
  it('should have all of the propagate flags', function() {
    for (var i = 0; i < propagateFlagNames.length; i++) {
      assert(grpc.propagate.hasOwnProperty(propagateFlagNames[i]),
             'call error missing: ' + propagateFlagNames[i]);
    }
  });
  it('should have all of the connectivity states', function() {
    for (var i = 0; i < connectivityStateNames.length; i++) {
      assert(grpc.connectivityState.hasOwnProperty(connectivityStateNames[i]),
             'connectivity status missing: ' + connectivityStateNames[i]);
    }
  });
});
