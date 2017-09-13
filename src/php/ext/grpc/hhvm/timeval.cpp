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

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "timeval.h"

#include "common.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/vm/native-data.h"

#include "grpc/grpc.h"

namespace HPHP {

/*****************************************************************************/
/*                           Time Value Data                                 */
/*****************************************************************************/

Class* TimevalData::s_pClass{ nullptr };
const StaticString TimevalData::s_ClassName( "Grpc\\Timeval" );

TimevalData::~TimevalData(void)
{
    destroy();
}

void TimevalData::init(const gpr_timespec& timeValue)
{
    // destroy any existing time values
    destroy();

    m_TimeValue = timeValue;
}

Class* const TimevalData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

void TimevalData::destroy(void)
{
  // TODO: cleanup
}

/*****************************************************************************/
/*                         HHVM Time Value Methods                           */
/*****************************************************************************/

void HHVM_METHOD(Timeval, __construct,
                 int64_t microseconds)
{
    HHVM_TRACE_SCOPE("Timeval construct") // Degug Trace

    TimevalData* const pTimeval{ Native::data<TimevalData>(this_) };
    pTimeval->init(gpr_time_from_micros(microseconds, GPR_TIMESPAN));
}

Object HHVM_METHOD(Timeval, add,
                   const Object& other_obj)
{
    HHVM_TRACE_SCOPE("Timeval add") // Degug Trace

    TimevalData* const pTimeval{ Native::data<TimevalData>(this_) };
    TimevalData* const pOtherTimeval{ Native::data<TimevalData>(other_obj) };

    Object newTimevalObj{ TimevalData::getClass() };
    TimevalData* const pNewTimeval{ Native::data<TimevalData>(newTimevalObj) };
    pNewTimeval->init(gpr_time_add(pTimeval->time(), pOtherTimeval->time()));

    return newTimevalObj;
}

Object HHVM_METHOD(Timeval, subtract,
                   const Object& other_obj)
{
    HHVM_TRACE_SCOPE("Timeval subtract") // Degug Trace

    TimevalData* const pTimeval{ Native::data<TimevalData>(this_) };
    TimevalData* const pOtherTimeval{ Native::data<TimevalData>(other_obj) };

    Object newTimevalObj{ TimevalData::getClass() };
    TimevalData* const pNewTimeval{ Native::data<TimevalData>(newTimevalObj) };
    pNewTimeval->init(gpr_time_sub(pTimeval->time(), pOtherTimeval->time()));

    return newTimevalObj;
}

int64_t HHVM_STATIC_METHOD(Timeval, compare,
                           const Object& a_obj,
                           const Object& b_obj)
{
    HHVM_TRACE_SCOPE("Timeval compare") // Degug Trace

    TimevalData* const pATimeval{ Native::data<TimevalData>(a_obj) };
    TimevalData* const pBTimeval{ Native::data<TimevalData>(b_obj) };

    int result{ gpr_time_cmp(pATimeval->time(), pBTimeval->time()) };

    return static_cast<int64_t>(result);
}

bool HHVM_STATIC_METHOD(Timeval, similar,
                        const Object& a_obj,
                        const Object& b_obj,
                        const Object& thresh_obj)
{
    HHVM_TRACE_SCOPE("Timeval similar") // Degug Trace

    TimevalData* const pATimeval{ Native::data<TimevalData>(a_obj) };
    TimevalData* const pBTimeval{ Native::data<TimevalData>(b_obj) };
    TimevalData* const pThresholdTimeval{ Native::data<TimevalData>(thresh_obj) };

    int result{ gpr_time_similar(pATimeval->time(), pBTimeval->time(),
                                 pThresholdTimeval->time()) };

    return (result != 0);
}

Object HHVM_STATIC_METHOD(Timeval, now)
{
    HHVM_TRACE_SCOPE("Timeval now") // Degug Trace

    Object newTimevalObj{ TimevalData::getClass() };
    TimevalData* const pNewTimeval{ Native::data<TimevalData>(newTimevalObj) };
    pNewTimeval->init(gpr_now(GPR_CLOCK_REALTIME));

    return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, zero)
{
    HHVM_TRACE_SCOPE("Timeval zero") // Degug Trace

    Object newTimevalObj{ TimevalData::getClass() };
    TimevalData* const pNewTimeval{ Native::data<TimevalData>(newTimevalObj) };
    pNewTimeval->init(gpr_time_0(GPR_CLOCK_REALTIME));

    return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, infFuture)
{
    HHVM_TRACE_SCOPE("Timeval infFuture") // Degug Trace

    Object newTimevalObj{ TimevalData::getClass() };
    TimevalData* const pNewTimeval{ Native::data<TimevalData>(newTimevalObj) };
    pNewTimeval->init(gpr_inf_future(GPR_CLOCK_REALTIME));

    return newTimevalObj;
}

Object HHVM_STATIC_METHOD(Timeval, infPast)
{
    HHVM_TRACE_SCOPE("Timeval infPast") // Degug Trace

    Object newTimevalObj{ TimevalData::getClass() };
    TimevalData* const pNewTimeval{ Native::data<TimevalData>(newTimevalObj) };
    pNewTimeval->init(gpr_inf_past(GPR_CLOCK_REALTIME));

    return newTimevalObj;
}

void HHVM_METHOD(Timeval, sleepUntil)
{
    HHVM_TRACE_SCOPE("Timeval sleepUntil") // Degug Trace

    TimevalData* const pTimeval{ Native::data<TimevalData>(this_) };
    gpr_sleep_until(pTimeval->time());
}

} // namespace HPHP
