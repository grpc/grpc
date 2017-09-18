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

#ifndef NET_GRPC_HHVM_GRPC_TIMEVAL_H_
#define NET_GRPC_HHVM_GRPC_TIMEVAL_H_

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif


#include "hphp/runtime/ext/extension.h"

#include "grpc/support/time.h"

namespace HPHP {

/*****************************************************************************/
/*                               Time Value Data                             */
/*****************************************************************************/

class TimevalData
{
public:
    TimevalData(void) : m_TimeValue{} {};
    ~TimevalData(void);
    TimevalData(const TimevalData& otherTimevalData) = delete;
    TimevalData(TimevalData&& otherTimevalData) = delete;
    TimevalData& operator=(const TimevalData& rhsTimevalData) = delete;
    TimevalData& operator=(TimevalData&& rhsTimevalData) = delete;
    void sweep(void);

    // interface functions
    void init(const gpr_timespec& timeValue);
    const gpr_timespec& time(void) const { return m_TimeValue; }
    static Class* const getClass(void);
    static const StaticString& className(void) { return s_ClassName; }

private:
    // helper functions
    void destroy(void);

    // member variables
    gpr_timespec m_TimeValue;
    static Class* s_pClass;
    static const StaticString s_ClassName;
};

/*****************************************************************************/
/*                            HHVM Time Value Mehods                         */
/*****************************************************************************/

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
