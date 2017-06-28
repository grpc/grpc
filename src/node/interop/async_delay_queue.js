/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
