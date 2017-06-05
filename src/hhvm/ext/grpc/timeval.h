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

#ifndef NET_GRPC_HHVM_GRPC_TIMEVAL_H_
#define NET_GRPC_HHVM_GRPC_TIMEVAL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"

#include <grpc/grpc.h>
#include <grpc/support/time.h>

namespace HPHP {

const StaticString s_Timeval("Timeval");

class Timeval {
  private:
    gpr_timespec* wrapped{nullptr};
  public:
    Timeval();
    ~Timeval();

    void init(gpr_timespec time);
    void sweep();
    gpr_timespec getWrapped();
};

void HHVM_METHOD(Timeval, __construct,
  int64_t microseconds);

Object HHVM_METHOD(Timeval, add,
  const Object& other_obj);

Object HHVM_METHOD(Timeval, subtract,
  const Object& other_obj);

int64_t HHVM_STATIC_METHOD(Timeval, compare,
  const Object& a_obj,
  const Object& b_obj);

bool HHVM_STATIC_METHOD(Timeval, similar,
  const Object& a_obj,
  const Object& b_obj,
  const Object& thresh_obj);

Object HHVM_STATIC_METHOD(Timeval, now);

Object HHVM_STATIC_METHOD(Timeval, zero);

Object HHVM_STATIC_METHOD(Timeval, infFuture);

Object HHVM_STATIC_METHOD(Timeval, infPast);

void HHVM_METHOD(Timeval, sleepUntil);

}

#endif /* NET_GRPC_HHVM_GRPC_TIMEVAL_H_ */
