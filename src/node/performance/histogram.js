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

/**
 * Histogram module. Exports the Histogram class
 * @module
 */

'use strict';

/**
 * Histogram class. Collects data and exposes a histogram and other statistics.
 * This data structure is taken directly from src/core/support/histogram.c, but
 * pared down to the statistics needed for client stats in
 * test/proto/benchmarks/stats.proto.
 * @constructor
 * @param {number} resolution The histogram's bucket resolution. Must be positive
 * @param {number} max_possible The maximum allowed value. Must be greater than 1
 */
function Histogram(resolution, max_possible) {
  this.resolution = resolution;
  this.max_possible = max_possible;

  this.sum = 0;
  this.sum_of_squares = 0;
  this.multiplier = 1 + resolution;
  this.count = 0;
  this.min_seen = max_possible;
  this.max_seen = 0;
  this.buckets = [];
  for (var i = 0; i < this.bucketFor(max_possible) + 1; i++) {
    this.buckets[i] = 0;
  }
}

/**
 * Get the bucket index for a given value.
 * @param {number} value The value to check
 * @return {number} The bucket index
 */
Histogram.prototype.bucketFor = function(value) {
  return Math.floor(Math.log(value) / Math.log(this.multiplier));
};

/**
 * Get the minimum value for a given bucket index
 * @param {number} The bucket index to check
 * @return {number} The minimum value for that bucket
 */
Histogram.prototype.bucketStart = function(index) {
  return Math.pow(this.multiplier, index);
};

/**
 * Add a value to the histogram. This updates all statistics with the new
 * value. Those statistics should not be modified except with this function
 * @param {number} value The value to add
 */
Histogram.prototype.add = function(value) {
  // Ensure value is a number
  value = +value;
  this.sum += value;
  this.sum_of_squares += value * value;
  this.count++;
  if (value < this.min_seen) {
    this.min_seen = value;
  }
  if (value > this.max_seen) {
    this.max_seen = value;
  }
  this.buckets[this.bucketFor(value)]++;
};

/**
 * Get the mean of all added values
 * @return {number} The mean
 */
Histogram.prototype.mean = function() {
  return this.sum / this.count;
};

/**
 * Get the variance of all added values. Used to calulate the standard deviation
 * @return {number} The variance
 */
Histogram.prototype.variance = function() {
  if (this.count == 0) {
    return 0;
  }
  return (this.sum_of_squares * this.count - this.sum * this.sum) /
      (this.count * this.count);
};

/**
 * Get the standard deviation of all added values
 * @return {number} The standard deviation
 */
Histogram.prototype.stddev = function() {
  return Math.sqrt(this.variance);
};

/**
 * Get the maximum among all added values
 * @return {number} The maximum
 */
Histogram.prototype.maximum = function() {
  return this.max_seen;
};

/**
 * Get the minimum among all added values
 * @return {number} The minimum
 */
Histogram.prototype.minimum = function() {
  return this.min_seen;
};

/**
 * Get the number of all added values
 * @return {number} The count
 */
Histogram.prototype.getCount = function() {
  return this.count;
};

/**
 * Get the sum of all added values
 * @return {number} The sum
 */
Histogram.prototype.getSum = function() {
  return this.sum;
};

/**
 * Get the sum of squares of all added values
 * @return {number} The sum of squares
 */
Histogram.prototype.sumOfSquares = function() {
  return this.sum_of_squares;
};

/**
 * Get the raw histogram as a list of bucket sizes
 * @return {Array.<number>} The buckets
 */
Histogram.prototype.getContents = function() {
  return this.buckets;
};

module.exports = Histogram;
