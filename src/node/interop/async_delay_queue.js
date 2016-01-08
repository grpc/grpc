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

var _ = require('lodash');

/**
 * This class represents a queue of callbacks that must happen sequentially,
 * each with a specific delay after the previous event.
 */
function AsyncDelayQueue() {
  this.queue = [];

  this.callback_pending = false;
}

/**
 * Run the next callback after its corresponding delay, if there are any
 * remaining.
 */
AsyncDelayQueue.prototype.runNext = function() {
  var next = this.queue.shift();
  var continueCallback = _.bind(this.runNext, this);
  if (next) {
    this.callback_pending = true;
    setTimeout(function() {
      next.callback(continueCallback);
    }, next.delay);
  } else {
    this.callback_pending = false;
  }
};

/**
 * Add a callback to be called with a specific delay after now or after the
 * current last item in the queue or current pending callback, whichever is
 * latest.
 * @param {function(function())} callback The callback
 * @param {Number} The delay to apply, in milliseconds
 */
AsyncDelayQueue.prototype.add = function(callback, delay) {
  this.queue.push({callback: callback, delay: delay});
  if (!this.callback_pending) {
    this.runNext();
  }
};

module.exports = AsyncDelayQueue;
