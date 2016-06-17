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
