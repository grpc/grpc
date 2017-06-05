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

#include <grpc/grpc.h>
#include <grpc/support/time.h>

namespace HPHP {

Timeval::Timeval() {}
Timeval::~Timeval() { sweep(); }

void Timeval::init(gpr_timespec time) {
  memcpy(wrapped, &time, sizeof(gpr_timespec));
}

void Timeval::sweep() {
  if (wrapped) {
    req::free(wrapped);
    wrapped = nullptr;
  }
}

gpr_timespec Timeval::getWrapped() {
  return *wrapped;
}

void HHVM_METHOD(Timeval, __construct,
  int64_t microseconds) {
  auto timeval = Native::data<Timeval>(this_);
  timeval->init(gpr_time_from_micros(microseconds, GPR_TIMESPAN));
}

Object HHVM_METHOD(Timeval, add,
  const Object& other_obj) {
  auto timeval = Native::data<Timeval>(this_);
  auto otherTimeval = Native::data<Timeval>(other_obj);

  auto newTimevalObj = create_object("Timeval", Array());
  auto newTimeval = Native::data<Timeval>(newTimevalObj);

  newTimeval->init(gpr_time_add(timeval->getWrapped(), otherTimeval->getWrapped()));

  return newTimevalObj;
}

Object HHVM_METHOD(Timeval, subtract,
  const Object& other_obj) {
  auto timeval = Native::data<Timeval>(this_);
  auto otherTimeval = Native::data<Timeval>(other_obj);
  auto newTimevalObj = create_object("Timeval", Array());
  auto newTimeval = Native::data<Timeval>(newTimevalObj);
  
  newTimeval->init(gpr_time_add(timeval->getWrapped(), otherTimeval->getWrapped()));

  return newTimevalObj;
}

int64_t HHVM_STATIC_METHOD(Timeval, compare,
  const Object& a_obj,
  const Object& b_obj) {
  auto aTimeval = Native::data<Timeval>(a_obj);
  auto bTimeval = Native::data<Timeval>(b_obj);

  long result = gpr_time_cmp(aTimeval->getWrapped(), bTimeval->getWrapped());

  return (uint64_t)result;
}

bool HHVM_STATIC_METHOD(Timeval, similar,
  const Object& a_obj,
  const Object& b_obj,
  const Object& thresh_obj) {
  auto aTimeval = Native::data<Timeval>(a_obj);
  auto bTimeval = Native::data<Timeval>(b_obj);
  auto thresholdTimeval = Native::data<Timeval>(thresh_obj);

  int result = gpr_time_similar(aTimeval->getWrapped(), bTimeval->getWrapped(), thresholdTimeval->getWrapped());

  return (bool)result;
}

Object HHVM_STATIC_METHOD(Timeval, now) {
  auto newTimevalObj = create_object("Timeval", Array());
  auto newTimeval = Native::data<Timeval>(newTimevalObj);
  newTimeval->init(gpr_now(GPR_CLOCK_REALTIME));

  return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, zero) {
  auto newTimevalObj = create_object("Timeval", Array());
  auto newTimeval = Native::data<Timeval>(newTimevalObj);
  newTimeval->init(gpr_time_0(GPR_CLOCK_REALTIME));

  return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, infFuture) {
  auto newTimevalObj = create_object("Timeval", Array());
  auto newTimeval = Native::data<Timeval>(newTimevalObj);
  newTimeval->init(gpr_inf_future(GPR_CLOCK_REALTIME));

  return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, infPast) {
  auto newTimevalObj = create_object("Timeval", Array());
  auto newTimeval = Native::data<Timeval>(newTimevalObj);
  newTimeval->init(gpr_inf_past(GPR_CLOCK_REALTIME));

  return newTimevalObj;
}

void HHVM_METHOD(Timeval, sleepUntil) {
  auto timeval = Native::data<Timeval>(this_);
  gpr_sleep_until(timeval->getWrapped());
}

} // namespace HPHP
