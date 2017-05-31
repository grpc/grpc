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

#include "timeval.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hhvm_grpc.h"

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/support/time.h>

/**
 * Constructs a new instance of the Timeval class
 * @param long $microseconds The number of microseconds in the interval
 */
void HHVM_METHOD(Timeval, __construct,
  long microseconds) {
  ...
}

/**
 * Adds another Timeval to this one and returns the sum. Calculations saturate
 * at infinities.
 * @param Timeval $other_obj The other Timeval object to add
 * @return Timeval A new Timeval object containing the sum
 */
Timeval& HHVM_METHOD(Timeval, add,
  Timeval& other_obj) {
  ...
}

/**
 * Subtracts another Timeval from this one and returns the difference.
 * Calculations saturate at infinities.
 * @param Timeval $other_obj The other Timeval object to subtract
 * @return Timeval A new Timeval object containing the diff 
 */
Timeval& HHVM_METHOD(Timeval, subtract,
  Timeval& other_obj) {
  ...
}

/**
 * Return negative, 0, or positive according to whether a < b, a == b,
 * or a > b respectively.
 * @param Timeval $a_obj The first time to compare
 * @param Timeval $b_obj The second time to compare
 * @return long
 */
long HHVM_METHOD(Timeval, compare,
  Timeval& a_obj,
  Timeval& b_obj) {
  ...
}

/**
 * Checks whether the two times are within $threshold of each other
 * @param Timeval $a_obj The first time to compare
 * @param Timeval $b_obj The second time to compare
 * @param Timeval $thresh_obj The threshold to check against
 * @return bool True if $a and $b are within $threshold, False otherwise
 */
bool HHVM_METHOD(Timeval, similar,
  Timeval& a_obj,
  Timeval& b_obj,
  Timeval& thresh_obj) {
  ...
}

/**
 * Returns the current time as a timeval object
 * @return Timeval The current time
 */
Timeval& HHVM_METHOD(Timeval, now) {
  ...
}

/**
 * Returns the zero time interval as a timeval object
 * @return Timeval Zero length time interval
 */
Timeval& HHVM_METHOD(Timeval, zero) {
  ...
}

/**
 * Returns the infinite future time value as a timeval object
 * @return Timeval Infinite future time value
 */
Timeval& HHVM_METHOD(Timeval, infFuture) {
  ...
}

/**
 * Returns the infinite past time value as a timeval object
 * @return Timeval Infinite past time value
 */
Timeval& HHVM_METHOD(Timeval, infPast) {
  ...
}

/**
 * Sleep until this time, interpreted as an absolute timeout
 * @return void
 */
void HHVM_METHOD(Timeval, sleepUntil) {
  ...
}
