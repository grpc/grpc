#!/usr/bin/env ruby

# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Histogram class for use in performance testing and measurement

class Histogram
  # Determine the bucket index for a given value
  # @param {number} value The value to check
  # @return {number} The bucket index
  def bucket_for(value)
    (Math.log(value)/Math.log(@multiplier)).to_i
  end
  # Initialize an empty histogram
  # @param {number} resolution The resolution of the histogram
  # @param {number} max_possible The maximum value for the histogram
  def initialize(resolution, max_possible)
    @resolution=resolution
    @max_possible=max_possible
    @sum=0
    @sum_of_squares=0
    @multiplier=1+resolution
    @count=0
    @min_seen=max_possible
    @max_seen=0
    @buckets=Array.new(bucket_for(max_possible)+1, 0)
  end
  # Add a value to the histogram. This updates all statistics with the new
  # value. Those statistics should not be modified except with this function
  # @param {number} value The value to add
  def add(value)
    @sum += value
    @sum_of_squares += value * value
    @count += 1
    if value < @min_seen
      @min_seen = value
    end
    if value > @max_seen
      @max_seen = value
    end
    @buckets[bucket_for(value)] += 1
  end
  def minimum
    @min_seen
  end
  def maximum
    @max_seen
  end
  def sum
    @sum
  end
  def sum_of_squares
    @sum_of_squares
  end
  def count
    @count
  end
  def contents
    @buckets
  end
end
