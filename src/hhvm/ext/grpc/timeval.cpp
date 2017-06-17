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
const StaticString TimevalData::s_className("Grpc\\Timeval");

IMPLEMENT_GET_CLASS(TimevalData);

TimevalData::TimevalData() { }
TimevalData::~TimevalData() { sweep(); }

void TimevalData::init(gpr_timespec time) {
  memcpy(&wrapped, &time, sizeof(gpr_timespec));
}

void TimevalData::sweep() {
  //if (&wrapped) {
    //free(wrapped);
    //&wrapped = nullptr;
  //}
}

gpr_timespec TimevalData::getWrapped() {
  return wrapped;
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
