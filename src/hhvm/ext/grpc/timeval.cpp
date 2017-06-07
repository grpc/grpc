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

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"

#include <stdbool.h>
#include <stdlib.h>

#include <grpc/grpc.h>
#include <grpc/support/time.h>

namespace HPHP {

Class* TimevalData::s_class = nullptr;
const StaticString TimevalData::s_className("Timeval");

IMPLEMENT_GET_CLASS(TimevalData);

TimevalData::TimevalData() {}
TimevalData::~TimevalData() { sweep(); }

void TimevalData::init(gpr_timespec time) {
  memcpy(&wrapped, &time, sizeof(gpr_timespec));
}

void TimevalData::sweep() {
  /*if (wrapped) {
    //free(wrapped);
    wrapped = nullptr;
  }*/
}

gpr_timespec TimevalData::getWrapped() {
  return wrapped;
  //gpr_timespec time;
  //memcpy(&time, wrapped, sizeof(gpr_timespec));
  //return time;
}

void HHVM_METHOD(Timeval, __construct,
  int64_t microseconds) {
  auto timeval = Native::data<TimevalData>(this_);
  timeval->init(gpr_time_from_micros(microseconds, GPR_TIMESPAN));
}

Object HHVM_METHOD(Timeval, add,
  const Object& other_obj) {
  auto timeval = Native::data<TimevalData>(this_);
  auto otherTimeval = Native::data<TimevalData>(other_obj);

  auto newTimevalObj = Object{TimevalData::getClass()};
  auto newTimeval = Native::data<TimevalData>(newTimevalObj);

  newTimeval->init(gpr_time_add(timeval->getWrapped(), otherTimeval->getWrapped()));

  return newTimevalObj;
}

Object HHVM_METHOD(Timeval, subtract,
  const Object& other_obj) {
  auto timeval = Native::data<TimevalData>(this_);
  auto otherTimeval = Native::data<TimevalData>(other_obj);
  auto newTimevalObj = Object{TimevalData::getClass()};
  auto newTimeval = Native::data<TimevalData>(newTimevalObj);
  
  newTimeval->init(gpr_time_sub(timeval->getWrapped(), otherTimeval->getWrapped()));

  return newTimevalObj;
}

int64_t HHVM_STATIC_METHOD(Timeval, compare,
  const Object& a_obj,
  const Object& b_obj) {
  auto aTimeval = Native::data<TimevalData>(a_obj);
  auto bTimeval = Native::data<TimevalData>(b_obj);

  long result = gpr_time_cmp(aTimeval->getWrapped(), bTimeval->getWrapped());

  return (uint64_t)result;
}

bool HHVM_STATIC_METHOD(Timeval, similar,
  const Object& a_obj,
  const Object& b_obj,
  const Object& thresh_obj) {
  auto aTimeval = Native::data<TimevalData>(a_obj);
  auto bTimeval = Native::data<TimevalData>(b_obj);
  auto thresholdTimeval = Native::data<TimevalData>(thresh_obj);

  int result = gpr_time_similar(aTimeval->getWrapped(), bTimeval->getWrapped(), thresholdTimeval->getWrapped());

  return (bool)result;
}

Object HHVM_STATIC_METHOD(Timeval, now) {
  auto newTimevalObj = Object{TimevalData::getClass()};
  auto newTimeval = Native::data<TimevalData>(newTimevalObj);
  newTimeval->init(gpr_now(GPR_CLOCK_REALTIME));

  return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, zero) {
  auto newTimevalObj = Object{TimevalData::getClass()};
  auto newTimeval = Native::data<TimevalData>(newTimevalObj);
  newTimeval->init(gpr_time_0(GPR_CLOCK_REALTIME));

  return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, infFuture) {
  auto newTimevalObj = Object{TimevalData::getClass()};
  auto newTimeval = Native::data<TimevalData>(newTimevalObj);
  newTimeval->init(gpr_inf_future(GPR_CLOCK_REALTIME));

  return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, infPast) {
  auto newTimevalObj = Object{TimevalData::getClass()};
  auto newTimeval = Native::data<TimevalData>(newTimevalObj);
  newTimeval->init(gpr_inf_past(GPR_CLOCK_REALTIME));

  return newTimevalObj;
}

void HHVM_METHOD(Timeval, sleepUntil) {
  auto timeval = Native::data<TimevalData>(this_);
  gpr_sleep_until(timeval->getWrapped());
}

} // namespace HPHP
