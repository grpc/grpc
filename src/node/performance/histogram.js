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
